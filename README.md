# recterm

## description

Read terminal escape sequences and output gif animation directly (almost vt102 compatible terminal emulation).

This program is based on [yaft](https://github.com/uobikiemukot/yaft).

## usage

```
$ echo -ne '256colors2.pl\n' | recterm > 256.gif
```

![256colors2.pl](https://raw.githubusercontent.com/uobikiemukot/recterm/master/sample/256.gif)

```
$ echo -ne 'cat netlab.sixel\n' | recterm > netlab.gif
```

![netlab.sixel](https://raw.githubusercontent.com/uobikiemukot/recterm/master/sample/netlab.gif)

```
$ echo -ne 'sl\n' | recterm > sl.gif
```

![sl](https://raw.githubusercontent.com/uobikiemukot/recterm/master/sample/sl.gif)

```
$ echo -ne "echo -n \"yes\nno\n\" | peco\n^N^P^N^P\n" | recterm > peco.gif
```

![peco](https://raw.githubusercontent.com/uobikiemukot/recterm/master/sample/peco.gif)


```
$ echo -ne "man man\nkkkkjjjjjjjjj" | recterm > man.gif
```

![man](https://raw.githubusercontent.com/uobikiemukot/recterm/master/sample/man.gif)
