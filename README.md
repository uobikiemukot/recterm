# recterm

## description

```
                          +----------------+
  input: key sequence --> |outside terminal| --> drawing screen
                          +----------------+
                                |  ^
                                v  |
                              +-------+
                              |recterm|      --> output: animation gif
                              +-------+
                                |  ^
                                v  | 
                          +---------------+
                          |pseudo terminal|
                          +---------------+
                                |  ^
                   key sequence v  | terminal escape sequence
                               +-----+
                               |shell|
                               +-----+
```

This program is based on [yaft](https://github.com/uobikiemukot/yaft).

## usage

```
$ recterm
```

(default output file: recterm.gif)

or

```
$ recterm output.gif
```

## tools

rec
:	the alternative of ttyrec (using valid terminal escape sequence)

rec2gif
:	convert record of rec into gif

recfilter
:	read key sequence from stdin/pipe and convert shell output into gif (non interactive version of recterm) 

## related program

-	[seq2gif](https://github.com/saitoha/seq2gif) by saitoha
	convert a ttyrec record into a gif animation directly (fork of recterm, using yaft's terminal emulation)

## license

GPLv3

Copyright (c) 2012 haru <uobikiemukot at gmail dot com>
