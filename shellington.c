#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>            //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
const char * sysname = "shellington";

//Defining the variables to change
#define N 40
#define M 100
#define PSMODULE "ps_traverse.ko"
#define FILEMODULE "file_list.ko"
//Creating the arrays for the dynamic array
char** shortNames; 
char** shortPath;
char** bookmarks;


void execute(char* name, char** args);
void pstraverse(int root, char* flag);
void filelist(char* filename);

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
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};
/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t * command)
{
	int i=0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background?"yes":"no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete?"yes":"no");
	printf("\tRedirects:\n");
	for (i=0;i<3;i++)
		printf("\t\t%d: %s\n", i, command->redirects[i]?command->redirects[i]:"N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i=0;i<command->arg_count;++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}


}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i=0; i<command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i=0;i<3;++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next=NULL;
	}
	free(command->name);
	free(command);
	return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
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
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters=" \t"; // split at whitespace
	int index, len;
	len=strlen(buf);
	while (len>0 && strchr(splitters, buf[0])!=NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len>0 && strchr(splitters, buf[len-1])!=NULL)
		buf[--len]=0; // trim right whitespace

	if (len>0 && buf[len-1]=='?') // auto-complete
		command->auto_complete=true;
	if (len>0 && buf[len-1]=='&') // background
		command->background=true;

	char *pch = strtok(buf, splitters);
	command->name=(char *)malloc(strlen(pch)+1);
	if (pch==NULL)
		command->name[0]=0;
	else
		strcpy(command->name, pch);

	command->args=(char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index=0;
	char temp_buf[1024], *arg;
	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch) break;
		arg=temp_buf;
		strcpy(arg, pch);
		len=strlen(arg);

		if (len==0) continue; // empty arg, go for next
		while (len>0 && strchr(splitters, arg[0])!=NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len>0 && strchr(splitters, arg[len-1])!=NULL) arg[--len]=0; // trim right whitespace
		if (len==0) continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|")==0)
		{
			struct command_t *c=malloc(sizeof(struct command_t));
			int l=strlen(pch);
			pch[l]=splitters[0]; // restore strtok termination
			index=1;
			while (pch[index]==' ' || pch[index]=='\t') index++; // skip whitespaces

			parse_command(pch+index, c);
			pch[l]=0; // put back strtok termination
			command->next=c;
			continue;
		}

		// background process
		if (strcmp(arg, "&")==0)
			continue; // handled before

		// handle input redirection
		redirect_index=-1;
		if (arg[0]=='<')
			redirect_index=0;
		if (arg[0]=='>')
		{
			if (len>1 && arg[1]=='>')
			{
				redirect_index=2;
				arg++;
				len--;
			}
			else redirect_index=1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index]=malloc(len);
			strcpy(command->redirects[redirect_index], arg+1);
			continue;
		}

		// normal arguments
		if (len>2 && ((arg[0]=='"' && arg[len-1]=='"')
			|| (arg[0]=='\'' && arg[len-1]=='\''))) // quote wrapped arg
		{
			arg[--len]=0;
			arg++;
		}
		command->args=(char **)realloc(command->args, sizeof(char *)*(arg_index+1));
		command->args[arg_index]=(char *)malloc(len+1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count=arg_index;
	return 0;
}
void prompt_backspace()
{
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index=0;
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
    new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
    // Those new settings will be set to STDIN
    // TCSANOW tells tcsetattr to change attributes immediately.
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);


    //FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state=0;
	buf[0]=0;
  	while (1)
  	{
		c=getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c==9) // handle tab
		{
			buf[index++]='?'; // autocomplete
			break;
		}

		if (c==127) // handle backspace
		{
			if (index>0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c==27 && multicode_state==0) // handle multi-code keys
		{
			multicode_state=1;
			continue;
		}
		if (c==91 && multicode_state==1)
		{
			multicode_state=2;
			continue;
		}
		if (c==65 && multicode_state==2) // up arrow
		{
			int i;
			while (index>0)
			{
				prompt_backspace();
				index--;
			}
			for (i=0;oldbuf[i];++i)
			{
				putchar(oldbuf[i]);
				buf[i]=oldbuf[i];
			}
			index=i;
			continue;
		}
		else
			multicode_state=0;

		putchar(c); // echo the character
		buf[index++]=c;
		if (index>=sizeof(buf)-1) break;
		if (c=='\n') // enter key
			break;
		if (c==4) // Ctrl+D
			return EXIT;
  	}
  	if (index>0 && buf[index-1]=='\n') // trim newline from the end
  		index--;
  	buf[index++]=0; // null terminate string

  	strcpy(oldbuf, buf);

  	parse_command(buf, command);

  	//print_command(command); // DEBUG: uncomment for debugging

    // restore the old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  	return SUCCESS;
}
int process_command(struct command_t *command);





int main()
{
	srand(time(NULL));
	shortNames = malloc(N* sizeof(char*));
	shortPath = malloc(N* sizeof(char*));
	bookmarks = malloc(N* sizeof(char*));
	for(int i = 0; i<N; i++){
		shortNames[i] = malloc((M+1)*sizeof(char));
		shortPath[i] = malloc((M+1)*sizeof(char));
		bookmarks[i] = malloc((M+1)*sizeof(char));
	}
	while (1)
	{
		struct command_t *command=malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code==EXIT) break;

		code = process_command(command);
		if (code==EXIT) break;

		free_command(command);
	}
	free(shortNames);
	free(shortPath);
	free(bookmarks);

	printf("\n");
	return 0;
}

int process_command(struct command_t *command)
{
	int r;
	if (strcmp(command->name, "")==0) return SUCCESS;

	if (strcmp(command->name, "exit")==0)
		return EXIT;

	if (strcmp(command->name, "cd")==0)
	{
		if (command->arg_count > 0)
		{
			r=chdir(command->args[0]);
			if (r==-1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			return SUCCESS;
		}
	}

	
    if (strcmp(command->name, "short")==0){
		// Checking the command lenght
        if(command->arg_count != 2){
            printf("Invalid short arguement.\n");
        } 
		// Creating variables for the inputs
        char* arg1 = command->args[0];
		char* nickName =command->args[1];
        if(strcmp(arg1, "jump")==0) {
			// Checking if the given short path is set
			int flag = 0;
			for(int i=0;i<N;i++) {	
				if(strlen(shortPath[i]) !=0){
					flag = 1;
					break;
				}
			}
			// Jumping to the directory if the given path is already set
			if(flag = 1){
				for (int i = 0; i < N; i++) {
					if (strcmp(nickName, shortNames[i])==0) {
						chdir(shortPath[i]);
						break;
					}
				}
			} else{
				printf("Invalid arguement for jump method.\n");
			}
        } else if (strcmp(arg1, "set") ==0){
			// Getting the current working directory
			char cwd[100];
			getcwd(cwd, sizeof(cwd));
			for(int i=0; i<N; i++){
				/* Checking if the nicknames that
				are already given can be overwritten */
				if(strcmp(shortNames[i], nickName) == 0){
					strcpy(shortPath[i], cwd);
					break;
				} else if(strlen(shortPath[i]) == 0) {
					// Creating the short name and path 
					strcpy(shortPath[i], cwd);
					strcpy(shortNames[i], nickName);
					break;
				}
			}
			// Uncomment for printing the shortcut and path
			// for(int i=0; i<N; i++){
			// 	if(strlen(shortPath[i]) != 0){
			// 		printf("%s %s\n", shortNames[i], shortPath[i]);
			// 	}
			// }
		} else { 
			printf("Invalid arguement for short function.\n");
		}
    }

	if (strcmp(command->name, "bookmark")==0) 	{
		// Getting the parameters
		char* param = command->args[0];
		
		if(strcmp(param,"-l")==0){
			// Printing the bookmarks
			for(int i=0; i<N; i++){
				if(strlen(bookmarks[i]) != 0){
					printf("%d %s\n",i, bookmarks[i]);
				}
			}
		} else if(param[0] == '\"') {
			// Checking the arguments aposthrope 
			char *argument = malloc(N* sizeof(char));
			for (int i = 0; i < command->arg_count; i++)
			{
				// Getting the bookmarked command together because of seperate arguments
				strcat(argument, command->args[i]);
				strcat(argument, " ");
			}
			for(int i=0; i<N; i++) {
				if(strlen(bookmarks[i]) == 0) {
					// Storing the commmand in the array list
					strcpy(bookmarks[i], argument);
					break;
				}
			}
			free(argument);
		} else if(command->arg_count == 1){
			for(int i=0; i<N; i++) {
				if(strlen(bookmarks[i]) == 0) {
					// Bookmarking tthe command even if it doesn't have aposthrope
					// Apostrophe may also be deleted when the input is taken
					strcpy(bookmarks[i], command->args[0]);
					break;
				}
			}
		}
		else if(command->arg_count == 2) {
			int index = atoi(command-> args[1]);
			if(strcmp(param, "-d")==0) {
				/* Deleting the bookmarked command while the 
				number of commands after the given index shifts up */ 
				char** temp;
				int count = 0;
				temp = malloc(N* sizeof(char*));
				for(int i = 0; i<N; i++) temp[i] = malloc((M+1)*sizeof(char));
				strcpy(bookmarks[index], "");
				for(int i = 0; i<N; i++) 
					if (strlen(bookmarks[i]) != 0) {
						strcpy(temp[count], bookmarks[i]);
						count++;
					}					
				for (int i = 0; i < N; i++) strcpy(bookmarks[i], temp[i]);
				free(temp);
			} else if(strcmp(param, "-i")==0 && strlen(bookmarks[index])!=0){
				if(bookmarks[index][0]=='\"'){
					// Creating the arguments for the execute command
					char commandArgs[100][100];
					char name[50]; 
					char newCommand[40];
					strcpy(newCommand, bookmarks[index]);
					int i = 0;
					// Tokenizing the arguments for the given command
					char *p = strtok(newCommand, " ");
					char *array[40];
					while (p != NULL)
					{
						array[i++] = p;
						p = strtok(NULL, " ");
					}
					strcpy(name, array[0]);
					int length = 0;
					i = 1;
					// Getting the commands into a pointer of pointers
					while(array[i]!=NULL){
						strcpy(commandArgs[i-1], array[i]);
						i++;  
					}
					length = i;
					commandArgs[i-2][(strlen(commandArgs[i-2])-1)] = '\0';
					char firstName[50];
					for(i = 1; i<strlen(name)+1;i++){
						firstName[i-1] = name[i]; // Deleting the apostrophes
					}
					char** commandParams = malloc(N* sizeof(char*));
					for(int i = 0; i<length-1; i++){
						commandParams[i] = malloc((M+1)*sizeof(char));
						strcpy(commandParams[i],commandArgs[i]); // Converting array of arrays to pointer of pointers
						printf("%s\n",commandParams[i]);
					}
					// Forking for the execute to prevent quitting 
					int pid = fork();
					if(pid == 0) {
						execute(firstName, commandParams);
					} else {
						wait(NULL);
					}
					free(commandParams);
				} else {
					char *commandName = bookmarks[index];
					char *argv[] = {commandName, NULL};
					int pid = fork();
					if(pid == 0){
						execute(commandName, argv);
					}else{
						wait(NULL);
					}
				}
			}
		} else {
			printf("Invalid argument for the command \"bookmark\"\n");
		}
 	}

	 if (strcmp(command->name, "remindme")==0) 	{
		// Getting the current working directory to create the necessary files
		char cwd[100];
		getcwd(cwd, sizeof(cwd));
		char* time = command->args[0];
		char* note = command->args[1];
		// Unccomment to see if the time is right
		//printf("%s, %s\n", time, note);
		
		if(command->arg_count == 2) {
			char** cronParams;
			char** cronFileParam;
			cronFileParam = malloc(1* sizeof(char*));
			for(int i = 0; i<1; i++) cronFileParam[i] = malloc((M+1)*sizeof(char));
			char croncwd[200];
			char sshcwd[200];
			// Creating the files
			strcpy(croncwd,cwd);
			strcat(croncwd,"/remindmecron.cron");
			strcpy(sshcwd, cwd);
			strcat(sshcwd, "/remindme.sh");
			printf("%s, %s\n",croncwd, sshcwd);
			strcpy(cronFileParam[0],croncwd);
			const char* delp;
			int ct = 1;
			cronParams = malloc(6* sizeof(char*));
			// Setting up the cron's parameters
			for(int i = 0; i<6; i++) cronParams[i] = malloc((M+1)*sizeof(char));
			delp = strtok (time, ".");
			while (delp != NULL)  {
				strcpy(cronParams[ct],delp);
				ct--;
				delp = strtok (NULL, ".");
			}
			strcpy(cronParams[2],"*");
			strcpy(cronParams[3],"*");
			strcpy(cronParams[4],"*");
			strcpy(cronParams[5],sshcwd);
			// Writing the information into the created files
			FILE *fp;
			fp = fopen(sshcwd, "w+");
			fprintf(fp,"notify-send \"Notification\" %s\n",note);
			fclose(fp);
			FILE *fp2;
			fp2 = fopen(croncwd, "w+");
			fprintf(fp2, "%s %s %s %s %s %s\n",cronParams[0],cronParams[1],cronParams[2],cronParams[3],cronParams[4],cronParams[5]);
			fclose(fp2);
			int child = fork();
			// Executing the crontab command with the file that has the instructions
			if(child==0){
				execute("crontab",cronFileParam);
			}else{
				wait(NULL);
				exit(0);
			}
			free(cronFileParam);
			free(cronParams);
		}else{
			printf("Invalid format for the command \"remindme\"\nUse \"remindme time note\"\nFor example: \"remindme 9.45 \"Time to take medicine\"\"\n");
		}
 	}
	// Awesome command 1: Calculator
	if (strcmp(command->name, "calculator")==0) 	{

		char* operation = command->args[0];
		char* firstNum = command->args[1];
		char* secondNum = command->args[2];
		
		
		if(command->arg_count == 3) {
			float firstNum = atof(command-> args[1]);
			float secondNum = atof(command-> args[2]);

			if(strcmp(operation, "+")==0) {
				printf("\nResult of this calculation: %f\n",(firstNum+secondNum));
			} else if(strcmp(operation, "-")==0){
				printf("\nResult of this calculation: %f\n",(firstNum-secondNum));
			} else if(strcmp(operation, "/")==0){
				printf("\nResult of this calculation: %f\n",(firstNum/secondNum));
			} else if(strcmp(operation, "*")==0){
				printf("\nResult of this calculation: %f\n",(firstNum*secondNum));
			}
	 	} else {
			printf("Invalid format for the command \"calculator\"\nUse \"calculator operation num1 num2\"\nFor example: \"calculator + 1 2\n");
	 	}
 	}
	// Awesome command 2: Guessing game
	if (strcmp(command->name, "guessinggame")==0) 	{


		int chances = 3;
		int selectedNumber = rand() % 11;
		int guessedNumber;
		printf("Welcome to the guessing game!\n");


		while(selectedNumber!=guessedNumber && chances != 0){
			printf("Pick a number between 0 and 10: \n");
			scanf("%d",&guessedNumber);
			if(selectedNumber<0 || selectedNumber>10){
				printf("Please enter a number in range: \n");
				scanf("%d",&guessedNumber);
			}else if(selectedNumber!=guessedNumber){
				chances--;
				printf("Try again.\n");
				printf("Pick a number between 0 and 10: \n");
				printf("Chances left: %d\n",chances);
			}
		}
		if(selectedNumber==guessedNumber){
			printf("You correctly guessed the number. You won!\n");
		}
		else if(chances == 0){
			printf("You lost the game!\n");
			printf("Selected number was: %d\n",selectedNumber);
		}
 	}

	if (strcmp(command->name, "pstraverse")==0) 	{
		pstraverse(atoi(command->args[0]),command->args[1]);
 	}
	if (strcmp(command->name, "filelist")==0) 	{
		filelist(command->args[0]);
 	}
	
	pid_t pid = fork();
	if(pid==0) // child
	{
		/// This shows how to do exec with environ (but is not available on MacOs)
	    // extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// increase args size by 2
		command->args=(char **)realloc(
			command->args, sizeof(char *)*(command->arg_count+=2));

		// shift everything forward by 1
		for (int i=command->arg_count-2;i>0;--i)
			command->args[i]=command->args[i-1];

		// set args[0] as a copy of name
		command->args[0]=strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count-1]=NULL;

		char *commandName = command->name;
		char **arguments = command->args;
		execute(commandName, arguments); 
		//execv(arguement, command->args); // exec+args+path
		exit(0);
		/// TODO: do your own exec with path resolving using execv()
	}
	else
	{
		if (!command->background)
			wait(0); // wait for child process to finish
		return SUCCESS;
	}

	// TODO: your implementation here

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}

void execute(char* name, char** args){
	// Executing the exceptions which are gcc and cd
	if(strcmp("cd", name)==0){
        chdir(args[0]);
		return;
    }
	
	if (strcmp("gcc", name)==0){
        execv("/usr/bin/gcc", args);
		return;
    }

	// Finding all paths
	char *PATH = getenv("PATH");
	int size = sizeof(PATH)/sizeof(PATH[0])+1;
	char *pathArray[size];

	// Tokenizing the paths into an array
	char *p = strtok(PATH,":");
	for (size_t i = 0; i < size; i++)  {
        pathArray[i] = p;
        p = strtok(NULL, ":");
    }
	int len = sizeof(pathArray)/sizeof(pathArray[0]);
	for (size_t i = 0; i < len; i++) {
		// Creating a buffer to use in execv 
		char buffer[50] = "";
		strcat(buffer,pathArray[i]);
		strcat(buffer, "/");
		strcat(buffer, name);
		// Checking if the given command is executable in any of the paths
		if(access(buffer, X_OK) == 0){
		// Executing the given command with arguements
		execv(buffer, args);
		}
	}
}

void pstraverse(int root, char *flag) {
	char rootStr[10];
	char flagStr[10];
	sprintf(rootStr, "pid=%d", root);
	sprintf(flagStr, "flag=%s", flag);
	int child = fork();
	if(child == 0){
		char *insArgs[] = {"/usr/bin/sudo","insmod",PSMODULE,rootStr,flagStr,0};
    	execv(insArgs[0], insArgs);
	}else{
		wait(NULL);
		char *rmArgs[] = {"/usr/bin/sudo","rmmod",PSMODULE,0};
    	execv(rmArgs[0], rmArgs);
	}
}

void filelist(char *filename) {
	char filenameStr[50];
	sprintf(filenameStr, "filename=%s", filename);
	int child = fork();
	if(child==0){
		char *insArgs[] = {"/usr/bin/sudo","insmod",FILEMODULE,filenameStr,0};
    	execv(insArgs[0], insArgs);
	}else{
		wait(NULL);
		char *rmArgs[] = {"/usr/bin/sudo","rmmod",FILEMODULE,0};
    	execv(rmArgs[0], rmArgs);
	}
}
