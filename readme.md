# TechShell

**Name:** Ahmed AlSabagh

## Description

TechShell is a small shell written in C that behaves like a simplified bash.
It prints a prompt showing the current working directory followed by `$ `,
reads a line, parses it, and runs the command in a forked child process using
`execvp()`. The parser understands double and single quotes, backslash-escaped
spaces, and input (`<`) and output (`>`) redirection. `cd` and `exit` are
built into the shell, because `cd` has to change the shell's own directory.
Errors are printed in the form `Error # (MESSAGE)` using `errno` and
`strerror()`. The program does not use `system()` or `popen()`.

## Clone, compile, and run

```
git clone <URL OF YOUR REPO>
cd <repo folder>
gcc -o techshell techshell.c
./techshell
```

Type `exit` (or press Ctrl-D) to leave the shell.

## What works

- Prompt with the current directory, updated after `cd`
- Running commands with options/arguments, by name or by absolute path
- `cd` (including errors for missing directory, no permission, and not a directory)
- Error messages such as `Error 2 (No such file or directory)`
- `>` and `<` redirection, separately or together (`wc -l < in.txt > out.txt`)
- Quotes (`"my file"`) and escaped spaces (`my\ file`)
- Blank or whitespace-only lines are ignored
- `exit`

Try it: `echo hi > a.txt`, `cat < a.txt`, `cd missing`, `horseface`.

## Not implemented

Pipes (`|`) are optional and are not implemented.
