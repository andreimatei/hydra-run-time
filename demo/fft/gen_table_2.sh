#!/bin/sh

echo '
pi    = 4 * a(1)
max_m = 16
max_n = 2 ^ max_m

for(i = 0; i < max_n; i++)
{
    print "    {", c(2 * i * pi / max_n), ",", -s(2 * i * pi / max_n), "},\n"
}
' | bc -l
