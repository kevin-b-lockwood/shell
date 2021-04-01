/**
 * A shell that enters an infinite loop which:
 *  - Prompts the user for the command to be executed with a specified prompt.
 *  - Allows the * user to enter any command along with the parameters the
 *    command requires (e.g., % ls foo.dat)
 *  - Has a built-in `cd` command that is not POSIX compliant, but does allow
 *    POSIX-style directory traversal with relative, absolute, and `~`-as-home
 *    paths.
 */
#include <sys/errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pwd.h>

#define BIG_NUM 2048 /* The most  arguments that a user can enter */
#define PATH_MAX 4096 /* The longest a path can be */
#define HOST_NAME_MAX 1024 /* The longest a hostname can be */

/**
 * Split up a string into tokens by delimiter, modify the token array to
 * contain all of the new tokens, modify token_count to be the length of the
 * resulting array.
 *
 * token_count is either set to some positive integer that defines the maximum
 * amount of elements to put.
 */
int tokenize(char* string, char* delimiter, char* tokens[], int* token_count)
{
    char* word = strtok(string, delimiter);
    int limit = *token_count;
    *token_count = 0;

    /* Tokenize while there are tokens left and while token_count is less than
     * the limit
     */
    while (word != NULL && (*token_count < limit - 1))
    {
        tokens[*token_count] = word;
        (*token_count)++;
        word = strtok(NULL, delimiter);
    }

    /* NULL terminate the token array */
    tokens[*token_count] = NULL;

    /* return -1 if token_count exceeds the limit. */
    return (*token_count < limit - 1) ? 0 : -1;
}


/**
 * Change directories to the given path. A NULL path goes to the home directory
 */
void cd(char* given_path)
{
    char* path[PATH_MAX]; /* the given path, element by element */

    char* old_path = getenv("OLDPWD");  /* last directory visited */
    char* home = getenv("HOME");        /* user's home dir */
    char* path_delimiter = "/";         /* divisor of path elements */

    char cur_path[PATH_MAX]; /* current working dir path */
    char new_path[PATH_MAX]; /* path under construction */

    int path_count;     /* How many elements are in the new path */
    int rooted = 0;     /* boolean if the path is absolute */

    /* Populate cur_path with the actual current path */
    getcwd(cur_path, PATH_MAX);

    /* null terminate the new path */
    new_path[0] = '\0';

    /* If cd recieved no path */
    if (!given_path)
    {
        strcat(new_path, home);
    } /* default path */
    else
    {
        /* If the path starts at root */
        rooted = given_path[0] == '/' ? 1 : 0;

        /* Split the given path into individual directories */
        path_count = BIG_NUM;
        tokenize(given_path, path_delimiter, path, &path_count);

        /* Substitute the home dir for ~ */
        if (!strcmp(path[0], "~"))
        {
            strcpy(new_path, home);
            path[0] = "";
        }
        /* toggling behavior for  - */
        else if (!strcmp(path[0], "-"))
        {
            strcpy(new_path, old_path);
            path_count = 0;
            puts(new_path);
        }
        /* behavior for absolute paths */
        else if (rooted)
        {
            strcpy(new_path, path_delimiter);
        }
        /* behavior for relative paths */
        else
        {
            strcat(new_path, cur_path);
        }

        /* Add the path items to the new path */
        for (int i = 0; i < path_count; i++)
        {
            strcat(new_path, path_delimiter);
            strcat(new_path, path[i]);
        }
    } /* Path parsing */



    /* Actually go to the new directory
     *
     * This structure is so that if any step fails, it stops
     * before it messes anything else up.
     */
    errno = 0;
    chdir(new_path);

    if (errno != 0)
    {
        perror("");
    }
    else
    {
        /* Update OLDPWD var */
        errno = 0;
        setenv("OLDPWD", cur_path, 1);
        if (errno != 0)
        {
            perror("");
        }
        else
        {
            /* Update PWD var */
            errno = 0;
            setenv("PWD", new_path, 1);
            if (errno != 0)
            {
                perror("");
            }
        }
    }
}


/**
 * A shell that runs until a user calls `exit`, at which point it will
 * `exit(0)`
 *
 * May be used to change directories and execute programs.
 */
int main(int argc, char* argv[])
{
    char* username;                 /* The current login name */
    char hostname[HOST_NAME_MAX];
    char prompt;                    /* The prompt to display */
    char delimiter[] = " \n";       /* what to tokenize on */

    /* Raw input information */
    char* input;        /* The input from user in this shell */
    int arg_count;      /* The count of arguments given in this shell */
    size_t input_len;   /* how many characters are in our input */

    /* Parsed input */
    char* command;                  /* The command to execute */
    char* command_args[BIG_NUM];    /* Max number of args a user can give */

    int child_pid = 0;

    while(1) /* Repeat forever */
    {
        uid_t uid = geteuid();
        struct passwd* pw = getpwuid(uid);
        char cur_path[PATH_MAX]; /* current working dir path */
        char* home = getenv("HOME");

        username  = (pw) ?  pw->pw_name : "ERROR";
        prompt = (strcmp(username, "root")) ? '%' : '#';
        gethostname(hostname, HOST_NAME_MAX);
        getcwd(cur_path, PATH_MAX);

        if (!strcmp(cur_path, home))
        {
            cur_path[0] = '~';
            cur_path[1] = '\0';
        }

        /* Display a prompt */
        printf("\033[1m╭─\033[0m");
        printf("\033[92;1m%s@%s\033[0m ", username, hostname);
        printf("\033[34;1m%s\033[0m \n", cur_path);

        printf("\033[1m╰─\033[0m");
        printf("\033[1m%c\033[0m ", prompt);

        /* Read a command from the user */
        getline(&input, &input_len, stdin);

        /* Parse the string into the command portion and
         * an array of pointers to argument strings using the blank
         * character as the separator. Note: the first argument
         * in the array of argument strings must be the same as
         * the command and the array must have a NULL pointer
         * after the last argument.
         */

        arg_count = BIG_NUM;
        if (tokenize(input, delimiter, command_args, &arg_count))
        {
            fprintf(stderr, "Exceeded length\n");
            /* Treat it as though nothing was given to the shell */
            arg_count = 0;
        }

        command = command_args[0];

        /* If the user entered 'exit' then call the exit() system call
         * to terminate the process
         */
        if (arg_count == 0);
        else if (!strcmp(command, "exit"))
        {
            exit(EXIT_SUCCESS);
        }
        /* Catch any path length overruns */
        else if (!strcmp(command, "cd")
            && (arg_count > 1)
            && (strlen(command_args[1]) > PATH_MAX))
        {
            fprintf(stderr, "ERROR: Path Invalid: too long");
        }
        /* Change directories */
        else if (!strcmp(command, "cd"))
        {
            cd(command_args[1]);
        } /* cd */
        /* Launch a new process */
        else
        {
            /* Fork a child process to execute the command and return
             * the result of the fork() in the child_pid variable so
             * we know whether we're now executing as the parent
             * or child and whether or not the fork() succeeded
             */

            errno = 0;
            child_pid = fork();

            if (!child_pid) /* We forked no child, we ARE the child */
            {
                /* We're now executing in the context of the child process.
                 * Use execvp() or execlp() to replace this program in
                 * the process' memory with the executable command the
                 * user has asked for.
                 */
                int ret = EXIT_SUCCESS;
                errno = 0;
                /* trim the ampersand off the end of the args */
                if (!strcmp(command_args[arg_count - 1], "&"))
                {
                    command_args[arg_count - 1] = NULL;
                }
                ret = execvp(command, command_args);

                if (errno != 0)
                {
                    perror("");
                }

                exit(ret);
            }
            else if (child_pid == -1)
            {
                /* An error occured during the fork - print it */
                perror("Fork failed: ");
                exit(EXIT_FAILURE);
            }
            else /* child_pid is the PID of the child */
            {
                /* We're still executing in the parent process.
                 * Wait for the child to finish before we prompt
                 * again.
                 */

                /* Let the child die (that sounds really morbid) */
                signal(SIGCHLD, SIG_IGN);

                char* final_arg = command_args[arg_count - 1];
                if (final_arg[strlen(final_arg - 1)] != '&')
                {
                    wait(NULL);
                }
                else
                {
                    printf("Job %d\n", child_pid);
                }
            }
        } /* fork */

        /* Clear out the argument array so that it doesn't wrap around into the
         * next command
         */
        for (int i = 0; i < arg_count; i++)
        {
            command_args[i] = NULL;
        }
        free(input);
        input = NULL;
    } /* while */
} /* my shell */
