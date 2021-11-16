#pragma once

#define WRAP(a, b) ((b) + ((a)%(b))) % (b)
#define ARRAYLEN(x) (sizeof(x) / sizeof(x[0]))
#define SGN(x) ((x) < 0 ? -1 : 1)
