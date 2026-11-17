// Name(s): Ahmed AlSabagh
// Description: TechShell, a simple bash-like shell. It shows a prompt with the
// current working directory, reads a command line, parses it (quotes, escaped
// spaces, < and > redirection), and runs it in a forked child with execvp().
// "cd" and "exit" are built in. Errors print as: Error # (MESSAGE)

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#define PATH_BUF 4096

// #define DEBUG   // uncomment for debug prints (remove before submitting!)

// Everything we learn from parsing one line of input
struct ShellCommand
{
    char **args;     // NULL-terminated array, exactly what execvp wants
    int argc;        // number of entries in args (not counting the NULL)
    char *inFile;    // file after '<', or NULL
    char *outFile;   // file after '>', or NULL
    int syntaxError; // 1 if the line could not be parsed (e.g. "ls >")
};

// A token is either a word or a redirect operator
struct Token
{
    char *text; // the word (NULL for operators)
    int op;     // 0 = word, '<' or '>' = operator
};

/* Print errors in the required format: Error # (MESSAGE) */
static void printError(int err)
{
    printf("Error %d (%s)\n", err, strerror(err));
    fflush(stdout);
}

/* Display the prompt: current directory, then "$ " */
void displayPrompt(void)
{
    char cwd[PATH_BUF];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        printf("%s$ ", cwd);
    else
        printf("$ ");
    fflush(stdout); // no newline, so we must flush by hand
}

/* Read one line from the user. Returns a malloc'd string without the
   newline, or NULL when input ends (Ctrl-D / end of file). */
char *getInput(void)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, stdin);
    if (n == -1)
    {
        free(line);
        return NULL;
    }
    if (n > 0 && line[n - 1] == '\n')
        line[n - 1] = '\0';
    return line;
}

/* Helper: add a token to the growing token array */
static void addToken(struct Token **toks, int *n, int *cap, char *text, int op)
{
    if (*n == *cap)
    {
        *cap *= 2;
        *toks = realloc(*toks, *cap * sizeof(struct Token));
        if (*toks == NULL)
        {
            perror("realloc");
            exit(1);
        }
    }
    (*toks)[*n].text = text;
    (*toks)[*n].op = op;
    (*n)++;
}

/* Helper: finish the word currently being built (if any) */
static void flushWord(struct Token **toks, int *n, int *cap,
                      char *buf, size_t *b, int *inWord)
{
    if (*inWord)
    {
        buf[*b] = '\0';
        addToken(toks, n, cap, strdup(buf), 0);
        *b = 0;
        *inWord = 0;
    }
}

/* Step 1 of parsing: split the line into words and < > operators.
   - whitespace separates words
   - "double" and 'single' quotes keep spaces together
   - a backslash makes the next character literal (e.g. "\ " is a space)
   - an unquoted < or > is an operator even with no spaces around it
   Returns the token count, or -1 on an unterminated quote. */
static int tokenize(const char *line, struct Token **out)
{
    size_t len = strlen(line);
    char *buf = malloc(len + 1);
    int cap = 8, n = 0, inWord = 0;
    size_t b = 0;
    char quote = 0;
    struct Token *toks = malloc(cap * sizeof(struct Token));
    if (buf == NULL || toks == NULL)
    {
        perror("malloc");
        exit(1);
    }

    for (size_t i = 0; i < len; i++)
    {
        char c = line[i];
        if (quote)
        {
            if (c == quote)
                quote = 0; // closing quote
            else if (quote == '"' && c == '\\' &&
                     (line[i + 1] == '"' || line[i + 1] == '\\'))
                buf[b++] = line[++i]; // \" or \\ inside double quotes
            else
                buf[b++] = c;
        }
        else if (c == '\\')
        {
            if (line[i + 1] != '\0')
                buf[b++] = line[++i]; // escaped character taken literally
            else
                buf[b++] = c;
            inWord = 1;
        }
        else if (c == '"' || c == '\'')
        {
            quote = c;
            inWord = 1; // so that "" still produces an (empty) argument
        }
        else if (isspace((unsigned char)c))
        {
            flushWord(&toks, &n, &cap, buf, &b, &inWord);
        }
        else if (c == '<' || c == '>')
        {
            flushWord(&toks, &n, &cap, buf, &b, &inWord);
            addToken(&toks, &n, &cap, NULL, c);
        }
        else
        {
            buf[b++] = c;
            inWord = 1;
        }
    }

    if (quote) // quote never closed
    {
        for (int i = 0; i < n; i++)
            free(toks[i].text);
        free(toks);
        free(buf);
        return -1;
    }
    flushWord(&toks, &n, &cap, buf, &b, &inWord);
    free(buf);
    *out = toks;
    return n;
}

/* Step 2 of parsing: turn the tokens into a ShellCommand */
struct ShellCommand parseInput(const char *input)
{
    struct ShellCommand cmd = {NULL, 0, NULL, NULL, 0};
    struct Token *toks = NULL;
    int n = tokenize(input, &toks);

    if (n < 0)
    {
        cmd.syntaxError = 1;
        return cmd;
    }

    cmd.args = calloc(n + 1, sizeof(char *)); // +1 for the NULL terminator

    for (int i = 0; i < n; i++)
    {
        if (toks[i].op)
        {
            // an operator must be followed by a filename (a word)
            if (i + 1 >= n || toks[i + 1].op)
            {
                cmd.syntaxError = 1;
                break;
            }
            char **target = (toks[i].op == '<') ? &cmd.inFile : &cmd.outFile;
            free(*target); // a later redirect replaces an earlier one
            *target = strdup(toks[i + 1].text);
            i++; // skip the filename token
        }
        else
        {
            cmd.args[cmd.argc++] = strdup(toks[i].text);
        }
    }

    for (int i = 0; i < n; i++)
        free(toks[i].text);
    free(toks);
    return cmd;
}

/* Free everything parseInput allocated */
static void freeCommand(struct ShellCommand *cmd)
{
    if (cmd->args)
    {
        for (int i = 0; i < cmd->argc; i++)
            free(cmd->args[i]);
        free(cmd->args);
    }
    free(cmd->inFile);
    free(cmd->outFile);
}

/* cd must be done by the shell itself: a child's chdir would vanish
   when the child exits. */
static void doCd(struct ShellCommand *cmd)
{
    const char *dir = (cmd->argc > 1) ? cmd->args[1] : getenv("HOME");
    if (dir == NULL)
        dir = "/";
    if (chdir(dir) != 0)
        printError(errno);
}

/* Run an external command: fork, set up redirects in the child, execvp.
   The child reports any failure to the parent through a pipe so that the
   PARENT prints the error (otherwise it could land inside a > file). */
static void runExternal(struct ShellCommand *cmd)
{
    int errPipe[2];
    if (pipe(errPipe) != 0)
    {
        printError(errno);
        return;
    }
    // close the write end automatically on a successful exec
    fcntl(errPipe[1], F_SETFD, FD_CLOEXEC);

    fflush(stdout); // don't let the child inherit unflushed output

    pid_t pid = fork();
    if (pid < 0)
    {
        printError(errno);
        close(errPipe[0]);
        close(errPipe[1]);
        return;
    }

    if (pid == 0) // ---------- child ----------
    {
        close(errPipe[0]);
        int err;

        if (cmd->inFile)
        {
            int fd = open(cmd->inFile, O_RDONLY);
            if (fd < 0)
            {
                err = errno;
                write(errPipe[1], &err, sizeof(err));
                _exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (cmd->outFile)
        {
            int fd = open(cmd->outFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0)
            {
                err = errno;
                write(errPipe[1], &err, sizeof(err));
                _exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        execvp(cmd->args[0], cmd->args);

        // only reached if exec failed
        err = errno;
        write(errPipe[1], &err, sizeof(err));
        _exit(1);
    }

    // ---------- parent ----------
    close(errPipe[1]);
    int childErr = 0;
    ssize_t got = read(errPipe[0], &childErr, sizeof(childErr));
    close(errPipe[0]);

    int status;
    waitpid(pid, &status, 0);

    if (got == (ssize_t)sizeof(childErr))
        printError(childErr);
}

/* Decide what to do with a parsed command */
void executeCommand(struct ShellCommand *cmd)
{
    if (cmd->syntaxError)
    {
        printError(EINVAL);
        return;
    }
    if (cmd->argc == 0) // blank line (or only redirects): nothing to run
        return;

    if (strcmp(cmd->args[0], "exit") == 0)
    {
        freeCommand(cmd);
        exit(0);
    }
    if (strcmp(cmd->args[0], "cd") == 0)
    {
        doCd(cmd);
        return;
    }
    runExternal(cmd);
}

int main() // MAIN
{
    char *input;
    struct ShellCommand command;

    // repeatedly prompt the user for input
    for (;;)
    {
        displayPrompt();

        input = getInput();
        if (input == NULL) // end of input (Ctrl-D)
        {
            printf("\n");
            break;
        }

        command = parseInput(input);
        executeCommand(&command);

        freeCommand(&command);
        free(input);
    }

    exit(0);
}
