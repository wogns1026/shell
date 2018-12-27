#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#define MAX_CMD_ARG 10

const char *prompt = "myshell> ";
char* cmdtmp[MAX_CMD_ARG]; // ';'으로 나뉘어지는 command를 저장
char* cmdpipe[MAX_CMD_ARG]; // 파이프로 나뉘어지는 command를 저장
char* cmdredir[MAX_CMD_ARG]; // 리다이렉션 확인을 위해 공백으로 나뉘어지는 문자열 저장
char* cmdvector[MAX_CMD_ARG];
char  cmdline[BUFSIZ];

void fatal(char *str) {
	perror(str);
	exit(1);
}

int makelist(char *s, const char *delimiters, char** list, int MAX_LIST) { // 명령어 구분해주는 함수	
	int i = 0;
	int numtokens = 0;
	char *snew = NULL;

	if ((s == NULL) || (delimiters == NULL)) return -1;

	snew = s + strspn(s, delimiters);	/* delimiters를 skip */
	if ((list[numtokens] = strtok(snew, delimiters)) == NULL) // snew를 delimiters를 기준으로 잘라서 list[numtokens]에 저장
		return numtokens;

	numtokens = 1;

	while (1) {
		if ((list[numtokens] = strtok(NULL, delimiters)) == NULL) // 직전 strtok 함수에서 처리했던 문자열에서 잘린 문자열만큼 이동한 다음 문자열부터 잘라서 list[numtokens]에 저장
			break;
		if (numtokens == (MAX_LIST - 1)) return -1;
		numtokens++; // 입력받은 명령어의 총 개수
	}
	return numtokens; // 명령어의 개수를 리턴함
}

void sigint_handler(int sig) { // 원래 쉘에서 ^c 입력시 자동 줄바꿈이 되는 것을 구현
	printf("\nmyshell> ");
	fflush(stdout); // 이걸 안쓰면 \n 까지만 출력되므로 버퍼에 있는 값을 모두 출력
}

void zombie_handler(int sig) { // SIGCHLD가 들어올 때 처리
	while (waitpid(-1, NULL, WNOHANG) > 0); // 좀비 프로세스를 처리
}

int background_check(char* cmd) { // 명령어에 백그라운드 표시 &가 포함되는지 확인하는 함수
	int flag = 0;
	for (int i = 0; i < strlen(cmd); i++) {
		if (cmd[i] == '&') { // 백그라운드면 1을 리턴
			cmd[i] = ' ';
			flag = 1;
		}
	}
	return flag; // 백그라운드가 아니면 0 리턴
}

void exec_redir(char* cmd) { // 리다이렉션 검사 후 명령어 실행하는 함수

	int cmdcnt = 0;
	int fd;
	
	cmdcnt = makelist(cmd, " \t", cmdredir, MAX_CMD_ARG); // 공백으로 명령어 구분
	for (int i = cmdcnt - 1; i >= 0; i--) {
		if (*cmdredir[i] == '<') {
			fd = open(cmdredir[i + 1], O_RDONLY | O_CREAT, 0644);
			// cmdredir[i+1] 파일을 읽기전용으로 생성
			dup2(fd, STDIN_FILENO); // 표준 입력으로 복제 => 파일의 데이터를 명령에 입력
			close(fd);
			cmdredir[i] = '\0'; // 명령어 수행을 위해 null로 초기화 
		}
		else if (*cmdredir[i] == '>') {
			// cmdredir[i + 1] 파일을 쓰기 전용으로 생성
			fd = open(cmdredir[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
			dup2(fd, STDOUT_FILENO); // 표준 출력으로 복제 => 명령의 결과를 파일에 저장
			close(fd);
			cmdredir[i] = '\0'; // 명령어 수행을 위해 null로 초기화
		}
	}
	
	execvp(cmdredir[0], cmdredir); // 명령어 수행
	fatal("main()");
}

void pipe_search(char* cmd) { // 파이프로 명령어를 구분하는 함수 

	int pfd[2], pipecnt = 0;
	int i = 0;
	pipecnt = makelist(cmd, "|", cmdpipe, MAX_CMD_ARG); // 파이프로 구분되는 개수
	for (i = 0; i < pipecnt - 1; i++) {
		pipe(pfd); // 파이프 파일디스크립터 설정
		switch (fork()) { // 파이프는 부모 자식 사이에서 사용됨
		case 0:
			close(pfd[0]); // 양방향을 사용하면 혼란이 발생하므로 사용하지 않는 파일디스크립터를 close
			dup2(pfd[1], STDOUT_FILENO);
			exec_redir(cmdpipe[i]);
		case -1:
			fatal("error");
		default:
			close(pfd[1]); // 양방향을 사용하면 혼란이 발생하므로 사용하지 않는 파일디스크립터를 close
			dup2(pfd[0], STDIN_FILENO);
		}
	}
	exec_redir(cmdpipe[i]);	// 가장 마지막 명령 수행
}

int main(int argc, char**argv) {
	int i = 0;
	int num = 0, cmdcnt = 0, idx = 0;
	int type = 0;
	pid_t pid;
	char cmdcopy[BUFSIZ];
	int background_chk = 0;

	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_handler = zombie_handler;
	act.sa_flags = SA_RESTART;
	sigaction(SIGCHLD, &act, NULL); // SIGCHLD 

	while (1) {
		fputs(prompt, stdout);

		signal(SIGINT, sigint_handler); // ctrl + 'c' 를 무시하고 줄바꿈하고 새로운 입력을 받음
		signal(SIGQUIT, SIG_IGN); // ctrl + '\' 를 무시
		signal(SIGTSTP, SIG_IGN); // ctrl + 'z'를 무시
		signal(SIGTTOU, SIG_IGN); // 제어권을 넘겨주기 위해 사용

		fgets(cmdline, BUFSIZ, stdin);
		if (strlen(cmdline) == 1) {
			continue;
		}
		cmdline[strlen(cmdline) - 1] = '\0';

		cmdcnt = makelist(cmdline, ";", cmdtmp, MAX_CMD_ARG); // ;를 통해 여러 명령을 구분하여 명령을 cmdtmp에 넣음

		for (int i = 0; i < cmdcnt; i++) {
			memcpy(cmdcopy, cmdtmp[i], strlen(cmdtmp[i]) + 1); // cmdcopy에 cmdtmp[i]를 복사하여 넣음
			background_chk = background_check(cmdcopy); // 백그라운드인지 아닌지 확인
			num = makelist(cmdtmp[i], " \t", cmdvector, MAX_CMD_ARG);

			if (!strcmp("cd", cmdvector[0])) { // cd 명령어가 입력된 경우
				if (num == 1) { // cd 만 입력될 경우 홈디렉토리로 이동해야함(제대로 작동하지 않음)
					cmdvector[1] = "~";
					chdir(cmdvector[1]);
				}
				else // 입력받은 디렉토리로 이동
					chdir(cmdvector[1]);
			}
			else if (!strcmp("exit", cmdvector[0])) { // exit 명령어가 입력된 경우
				exit(1); // 프로세스 종료
			}
			else {

				if (background_chk == 1) // 백그라운드면 type = 0으로 설정
					type = 0;
				else // 포어그라운드면 type = 1로 설정
					type = 1;

				switch (pid = fork()) {
				case 0:
					// 자식 프로세스는 ^c, ^\, ^z에 반응하도록 함(default action을 취하게 함)
					signal(SIGINT, SIG_DFL);
					signal(SIGQUIT, SIG_DFL);
					signal(SIGTSTP, SIG_DFL);
					setpgid(0, 0);
					if (type == 1) { // 포어그라운드 일때 제어권 획득
						tcsetpgrp(STDIN_FILENO, getpgid(0));
					}

					pipe_search(cmdcopy); // 파이프 명령어가 쓰였는지 탐색
				case -1: // 에러 처리
					fatal("main()");
				default:
					if (type == 0) // 백그라운드이면 자식이 끝날 때까지 기다리지 않음
						break;
					else if (type == 1) { // 포어그라운드
						waitpid(pid, NULL, 0); // 자식 프로세스가 끝날때까지 기다림
						tcsetpgrp(STDIN_FILENO, getpgid(0)); // 기다렸다가 제어권 회수
					}
				}
			}

		}
	}
}