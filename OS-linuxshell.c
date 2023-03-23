#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <stdbool.h>  

#include <fcntl.h>

const char *sysname = "shellax";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : 
"no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left 
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left 
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c = malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;


    if (arg[0] == '<') {
       redirect_index = 0;
      
      }

    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }


    if (redirect_index != -1) {
      command->redirects[redirect_index] = malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;
  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}
int process_command(struct command_t *command);
int main() {
  while (1) {
    struct command_t *command = malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}

/**
 * @param command_t
 * @return
 */
int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[0]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }
   
  if(strcmp(command->name, "uniq")){
  char *filename = command->args[0];
  txt_array(filename);
   }
   
   
   
  if(strcmp(command->name, "wiseman")) {
  
         int time = command->args[0];
         FILE *fortune = fopen( "fortune.txt" , "w+" );
         fputs("*/%d * * * * fortune -a | speak)\"\n",fortune);
         char *run[] = {"crontab", "fortune.txt"};
         char *cmd = "/usr/bin/crontab";
    
         int pid1;
         pid1 = fork();
         //forking handle process
         if (pid1 > 0 ){
         wait(NULL); 
         } else if (pid1 == 0) { 
         execvp(cmd,run);
         printf("It is working the quote will be loaded in time \n");
         } else {
         printf ("~~ERROR~~");
         }
       
         fclose(fortune);
  
  }
  
 
       if (strcmp(command->name , "irem" ) ) {
       char input;
       printf("Enter the file to be copied");
       scanf("%s" , &input);
       irem(command->args[0] , input);
    
       }   

     
      if (strcmp(command->name , "sedef") {
      char input2;
      printf("Enter the directory: ");
      scanf("%s" , &input2);
      sedef(command->args[0] , input2);
  
     } 
  
  pid_t pid = fork();
  if (pid == 0) // child
  {
    /// This shows how to do exec with environ (but is not available on 
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // 
    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the 
    // as required by exec

    // increase args size by 2
    command->args = (char **)realloc(
        command->args, sizeof(char *) * (command->arg_count += 2));

    // shift everything forward by 1
    for (int i = command->arg_count - 2; i > 0; --i)
      command->args[i] = command->args[i - 1];

    // set args[0] as a copy of name
    command->args[0] = strdup(command->name);
    // set args[arg_count-1] (last) to NULL
    command->args[command->arg_count - 1] = NULL;

    if (command->redirects[1] != NULL){

    int fd;
    fd = open(command->redirects[1], O_CREAT | O_RDWR | O_TRUNC, 0777);
    dup2(fd, STDOUT_FILENO);
    close(fd);

    return 1;

    }
    else if(command->redirects[2] != NULL) {
    
    int fd;
    fd = open(command->redirects[2], O_CREAT | O_RDWR | O_APPEND, 0777);
    dup2(fd, STDOUT_FILENO);
    close(fd);

    return 1;

    } else if(command->redirects[0] != NULL) {
    
    int fd;
    fd = open(command->args[0],  O_RDONLY, 0777);
    dup2(fd, STDOUT_FILENO);
    close(fd);

    return 1;

   }
    


    
    // TODO: do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
    execvp(command->name, command->args); // exec+args+path

    char *path = getenv("PATH");
    char splitter[] = ":";
    char tokenizer_element = strtok(path,splitter);
      

       while(tokenizer_element != NULL) {   
  
       int length = strlen(tokenizer_element) + (strlen(command->name)+1);
      char full[length];

      const *char_last = tokenizer_element;
    
       if(char_last != 0 ){
       char_last++;
       } 
        else { if(strcmp(char_last , "/") != 0){
        strcat(full, "/");
        }

      strcat(full, command->name);
      execv(full, command->args);
      tokenizer_element = strtok(NULL, ":");
    
        }
        }

   
    
      exit(0);
    } else {
     // TODO: implement background processes here
     // wait for child process to finish

     if (!command->background)
     wait(0); // wait for child process to finish

    return SUCCESS;
    }

  // TODO: your implementation here

  printf("-%s: %s: command not found\n", sysname, command->name);
  return UNKNOWN;
}

char *txt_array(char *filename){

char *file_array = malloc(10000);

File *file;
file = fopen(filename,"r");
fseek(file, 0, SEEK_SET);

fgets(file_array , 10000 , file);
puts(file_array);
fclose(file);


int array_length = sizeof(file_array)/sizeof(file_array[0]);

char *common_array[array_length];
char *lastChar = file_array[0];
common_array[0] = file_array[0];
int  size = 1;
int var_size= 1;

for(int i = 1; i<array_length;i++){
   for(int j = 0; j<array_length;j++){
   
       if(lastChar == file_array[i]){
         var_size++;
         continue;
      }
      else {
         printf("%s %d",*lastChar,var_size);
         var_size = 0;
         common_array[size] = file_array[i];
         size++;
         lastChar = file_array[i];
      }
}
}




for(int m=0;m<array_length;m++){
printf("%s",common_array[m]);
}

}




    //CUSTOM1~Boran Barut
    void irem(char* out , char* in)  {
    char character;

    FILE *reader = fopen(out,"r");
    FILE *writer = fopen(in,"w"); 

    while(fscanf(reader,"%c",character) != EOF) {
          fprintf(writer ,"%c",character);
    }
    
    fclose(reader);
    fclose(writer);
    } 
   
    //CUSTOM2~EFE Boyraz       
    void sedef (char* basePath , char* extraWay ){
    int length1 = strlen(basePath);
    int length2 = strlen(extraWay);

    char* returned = (char*)malloc((sizeof(char) * length1) + 
(sizeof(char) * length2) + 128);


    for(int i = 0; i < length1; i++){
        returned[i] = basePath[i];
    }
    returned[length1] = '/';


    for(int i = 0; i < length2 ; i++){
        returned[i + length1 + 1] =  extraWay [i];
    }
    
    //finish line for char in path names combination 
    returned[length1 + length2 + 1] = '\0';
    
    DIR *currentPath;
    currentPath = opendir(".");
    currentPath = returned;
    }



    //SEARCH FILE
    void search_file(char* fileName , char* directory) {
    DIR *openP;
    struct dirent *readP;

    if  ((openP = opendir(directory)) == NULL ) {
    return;
    }
    char path[1024];

    while ( (readP = readdir(directory)) == NULL ) {
    if ( readP->d_type == DT_DIR){
    if ( strcmp( readP->d_name,fileName) == 0 ) {
    strcat( path , "/");
    strcat (path , readP->d_name);
    printf("%s:\n" , path);
    }
    search_file ( fileName , path);
    }   else {
    if ( strcmp( readP -> d_name ,fileName) == 0 ) {
    strcat( path , "/");
    strcat (path , readP->d_name);
    printf("%s:\n" , path);
    }
    }
    free(path);
    }
    return;
    }

    




