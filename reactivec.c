/* gcc -std=c99 reactivec.c -o reactivec */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#define rac_signal() rac_signal_add(NULL, 0, 0)
#define rac_observe(x) rac_signal_add(&x, x, sizeof(x))
#define rac_observep(x) rac_signal_add(&x, (intptr_t)x, 0)
#define rac_emit(s, x) rac_signal_emit(s, x, sizeof(x))
#define rac_emitp(s, x) rac_signal_emit(s, (intptr_t)x, 0)

struct rac_signal {
   const void *current;
   intptr_t old, emit;
   size_t size;
};

typedef void (*rac_function)(struct rac_signal *signal);
struct rac_bind {
   rac_function function;
   struct rac_signal *signal;
};

struct rac_array {
   void **ptr;
   size_t items, allocated;
};

struct rac_machine {
   struct rac_array binds;
   struct rac_array signals;
} rac_machine = {
   { NULL, 0, 0 },
   { NULL, 0, 0 },
};

bool
rac_array_grow(struct rac_array *array)
{
   assert(array);

   void *tmp;
   if (!(tmp = realloc(array->ptr, array->allocated + 32 * sizeof(void*))))
      return false;

   array->ptr = tmp;
   array->allocated += 32;
   return true;
}

bool
rac_array_shrink(struct rac_array *array)
{
   assert(array);
   assert(array->items <= array->allocated - 32);

   void *tmp;
   if (!(tmp = realloc(array->ptr, array->allocated - 32 * sizeof(void*))))
      return false;

   array->ptr = tmp;
   array->allocated -= 32;
   return true;
}

void
rac_array_flush(struct rac_array *array)
{
   free(array->ptr);
   array->ptr = NULL;
   array->items =  array->allocated = 0;
}

void*
rac_array_iter(struct rac_array *array, size_t *iter)
{
   assert(array && iter);

   if (*iter >= array->items)
      return NULL;

   return array->ptr[(*iter)++];
}

void*
rac_array_add(struct rac_array *array, void *item)
{
   assert(array && item);

   if (array->allocated <= array->items && !rac_array_grow(array))
      return NULL;

   array->ptr[array->items++] = item;
   return item;
}

void
rac_array_remove(struct rac_array *array, void *item)
{
   assert(array && item);
   void *current;

   for (size_t iter = 0; (current = rac_array_iter(array, &iter));) {
      if (current != item)
         continue;

      if (iter < array->items)
         memmove(&array->ptr[iter - 1], &array->ptr[iter], (array->items - (iter - 1)) * sizeof(void*));

      if (array->items > 0)
         array->items--;

      break;
   }

   if (!array->items) {
      rac_array_flush(array);
   } else if (array->items < array->allocated - 32) {
      rac_array_shrink(array);
   }
}

struct rac_signal*
rac_signal_add(void *ptr, intptr_t value, size_t size)
{
   struct rac_signal *signal = calloc(1, sizeof(struct rac_signal));

   if (!signal)
      return NULL;

   signal->current = ptr;
   signal->old = value;
   signal->size = size;
   return rac_array_add(&rac_machine.signals, signal);
}

void
rac_signal_remove(struct rac_signal *signal)
{
   assert(signal);
   rac_array_remove(&rac_machine.signals, signal);
   free(signal);
}

void
rac_signal_reset(struct rac_signal *signal)
{
   assert(signal);

   if (!signal->current) {
      signal->old = 0;
      return;
   }

   if (signal->size) {
      memcpy(&signal->old, signal->current, signal->size);
   } else {
      signal->old = (intptr_t)*(void**)signal->current;
   }
}

bool
rac_signal_should_emit(struct rac_signal *signal)
{
   assert(signal);

   if (!signal->current)
      return (signal->old != 0);

   return (signal->size ? memcmp(signal->current, &signal->old, signal->size) : (intptr_t)*(void**)signal->current != signal->old);
}

void
rac_signal_emit(struct rac_signal *signal, intptr_t value, size_t size)
{
   assert(signal);
   signal->current = &signal->emit;
   signal->old = 0;
   signal->emit = value;
   signal->size = size;
}

const void*
rac_signal_value(struct rac_signal *signal)
{
   assert(signal);

   if (!signal->current)
      return NULL;

   return (signal->size ? signal->current : *(void**)signal->current);
}

void
rac_bind_call(struct rac_bind *bind)
{
   assert(bind);
   bind->function(bind->signal);
}

void
rac_advance(void)
{
   struct rac_bind *bind;
   for (size_t iter = 0; (bind = rac_array_iter(&rac_machine.binds, &iter));) {
      if (!rac_signal_should_emit(bind->signal))
         continue;

      rac_bind_call(bind);
   }

   struct rac_signal *signal;
   for (size_t iter = 0; (signal = rac_array_iter(&rac_machine.signals, &iter));)
      rac_signal_reset(signal);
}

void
rac_flush(void)
{
   void *item;
   for (size_t iter = 0; (item = rac_array_iter(&rac_machine.binds, &iter));)
      free(item);

   for (size_t iter = 0; (item = rac_array_iter(&rac_machine.signals, &iter));)
      free(item);

   rac_array_flush(&rac_machine.binds);
   rac_array_flush(&rac_machine.signals);
}

void
rac_bind_remove(struct rac_bind *bind)
{
   assert(bind);
   rac_array_remove(&rac_machine.binds, bind);
   free(bind);
}

struct rac_bind*
rac_call_on_signal(rac_function function, struct rac_signal *signal)
{
   struct rac_bind *bind = calloc(1, sizeof(struct rac_bind));

   if (!bind)
      return NULL;

   bind->function = function;
   bind->signal = signal;
   return rac_array_add(&rac_machine.binds, bind);
}

static void
xchanged(struct rac_signal *signal)
{
   assert(signal);
   printf("x changed to %d\n", *(int*)rac_signal_value(signal));
}

static void
ychanged(struct rac_signal *signal)
{
   assert(signal);
   printf("y changed to %s\n", (char*)rac_signal_value(signal));
}

static struct rac_signal *input_signal;
static struct rac_signal *exception_signal;
static int user_is_not_idiot = 1;

static void
side_effect(struct rac_signal *signal)
{
   printf("-!- ERROR: %s\n", (char*)rac_signal_value(signal));
}

static void
getinput(void)
{
   printf("-!- Are you a idiot? [y/n]?\n");
   char ch = getchar();
   rac_emit(input_signal, ch);
   while (ch != '\n') ch = getchar();
}

static void
gotinput(struct rac_signal *signal)
{
   user_is_not_idiot = (*(char*)rac_signal_value(signal) != 'y');

   if (user_is_not_idiot)
      rac_emitp(exception_signal, "YOU ARE ばか！");
}

int
main(void)
{
   const char *wtf[] = {
      "why",
      "u",
      "no",
      "like",
      "lolcats",
      "!?"
   };

   int x = 5;
   const char *y = "I don't like lolcats";

   input_signal = rac_signal();
   exception_signal = rac_signal();

   struct rac_bind *bind1 = rac_call_on_signal(xchanged, rac_observe(x));
   struct rac_bind *bind2 = rac_call_on_signal(ychanged, rac_observep(y));

   for (int i = 0; i < 25; ++i) {
      x = i * 35 & i;
      y = wtf[i % 6];
      rac_advance();
   }

   rac_bind_remove(bind1);
   rac_bind_remove(bind2);

   struct rac_bind *bind3 = rac_call_on_signal(gotinput, input_signal);
   struct rac_bind *bind4 = rac_call_on_signal(side_effect, exception_signal);

   while (user_is_not_idiot) {
      getinput();
      rac_advance();
   }

   rac_signal_remove(input_signal);
   rac_flush();
}

/* vim: set ts=8 sw=3 tw=0 :*/
