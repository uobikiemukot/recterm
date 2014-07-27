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
