#pragma once
#define GRND_NONBLOCK 1
#define GRND_RANDOM 2
ssize_t getrandom(void *ptr, size_t len, unsigned int flags);
