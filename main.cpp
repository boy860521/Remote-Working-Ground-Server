#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

struct twoInt{
	int ti[2];
};

struct threeInt{
	int thi[3];
};

struct fourInt{
    int fi[4];
};

struct Userinfo{
	int socketfd;
	char nickname[50];
	char IPandPort[50];
	string path;
};

const char who_row[] = {"<ID>	<nickname>	<IP/port>	<indicate me>\n"};
const char welcome_message[] = {"****************************************\n** Welcome to the information server. **\n****************************************\n"};
const int MAX_CLIENT_NUMBER = 30;

vector<vector<string>> WC;  // whole commands
string oneLine;
int serverSocketfd, forClientSocketfd;
struct Userinfo userInfo[MAX_CLIENT_NUMBER];
int clientNumber;
vector<threeInt> userPipeList;	// index for userPipe
vector<twoInt> userPipe;
vector<fourInt> NP;        // numbered pipes' readfd, numbered pipes' writefd, time, whose
int writeToWhichPipe;
vector<int> whichCommandOutStderrToPipe;
bool needToWaitAllChildProcessTerminated;
vector<pid_t> SRCPP;           // still running child processes's pid
int storedStd[3];

void printWC(){
	for(int i = 0; i < WC.size(); i++){
		for(int j = 0; j < WC[i].size(); j++)
			cout<<"WC["<<i<<"]["<<j<<"] = "<<WC[i][j]<<", ";
		cout<<endl;
	}
	return;
}

void printNP(){
    for(int i = 0; i < NP.size(); i++)
        cout<<"NP["<<i<<"][0] = "<<NP[i].fi[0]<<", NP["<<i<<"][1] = "<<NP[i].fi[1]<<", NP["<<i<<"][2] = "<<NP[i].fi[2]<<", NP["<<i<<"][3] = "<<NP[i].fi[3]<<endl;
    return;
}
bool is_integer(string s){
    char c;
    int l = s.length();
    if(l == 0) return false;
    for(int i = 0; i < l; i++){
        c = s[i];
        if(c < 48 || c > 57)
            return false;
    }
    return true;
}
char *to_c_str(string I){
    int len = I.length();
    char *O = new char(len+1);
    for(int i = 0; i < len; i++)
		O[i] = I[i];
	O[len] = '\0';
	return O;
}

void broadcast(char content[]){
	for(int i = 0; i < MAX_CLIENT_NUMBER; i++){
		if(userInfo[i].socketfd != -1){
			write(userInfo[i].socketfd, content, strlen(content));
		}
	}
}

void numbered_pipe_routing(int number){
    bool thereIsPipeToUse = false;
    int NPSize = NP.size();

    for(int i = 0; i < NPSize; i++){
        if(NP[i].fi[3] == forClientSocketfd && NP[i].fi[2] == number){
            thereIsPipeToUse = true;
            writeToWhichPipe = i;
        }
    }
    if(!thereIsPipeToUse){
        struct fourInt tempFourInt;
        int fds[2];
        pipe(fds);
        tempFourInt.fi[0] = fds[0];
        tempFourInt.fi[1] = fds[1];
        tempFourInt.fi[2] = number;
        tempFourInt.fi[3] = forClientSocketfd;
        NP.push_back(tempFourInt);
        writeToWhichPipe = NPSize;
    }
    return;
}
bool read_one_line(){
    vector<string> OIC; // one individual command
    string tempString;
    int eye, oneLineLength;

	// oneLine will last for the pipe notification
    getline(cin, oneLine);

    if(oneLine.empty()) return true;
    while(oneLine[oneLine.length() - 1] == '\n' || oneLine[oneLine.length() - 1] == '\r') oneLine.pop_back();
    oneLineLength = oneLine.length();
    if(oneLineLength == 0) return true;

	// initialize some parameters
    WC.clear();
    writeToWhichPipe = -1;
    whichCommandOutStderrToPipe.clear();
    needToWaitAllChildProcessTerminated = true;

    for(int i = 0; i < oneLineLength; i++){
        if(oneLine[i] == ' '){
            if(tempString.empty()){
                continue;
            }
            else{
                OIC.push_back(tempString);
                tempString.clear();
                continue;
            }
        }
        else if(oneLine[i] == '|' || oneLine[i] == '!'){
            if(i > 0){
                if(oneLine[i-1] != ' '){
                    tempString += oneLine[i];
                    continue;
                }
            }
            if(oneLine[i] == '!'){
                whichCommandOutStderrToPipe.push_back(WC.size());
            }
            WC.push_back(OIC);
            OIC.clear();
            if(i+1 < oneLineLength){
                i++;
                if(oneLine[i] == ' '){
                    continue;
                }
                else{
                    for( ;oneLine[i] != ' ' && i < oneLineLength; i++)
                        tempString += oneLine[i];
                    if(is_integer(tempString)){
                        numbered_pipe_routing(stoi(tempString));
                        tempString.clear();
                        needToWaitAllChildProcessTerminated = false;
                        break;
                    }
                    else{
                        cerr<<"Numbered pipe's number is not positive integer!"<<endl;
                        break;
                    }
                }
            }
        }
        else{
            tempString += oneLine[i];
        }
    }
    if(!tempString.empty()) OIC.push_back(tempString);
    if(!OIC.empty()) WC.push_back(OIC);
    return false;
}

void one_line_have_been_entered(){

	// adjust the numbered pipe list
    for(int i = 0; i < NP.size(); i++){
        if(NP[i].fi[3] == forClientSocketfd){
            if(NP[i].fi[2] < -1){
                close(NP[i].fi[0]);
                close(NP[i].fi[1]);
                NP.erase(NP.begin() + i);
                i--;
                continue;
            }
            NP[i].fi[2]--;
        }
    }
    return;
}

int read_from_which_pipe(){
    int NPSize = NP.size();

    for(int i = 0; i < NPSize; i++){
        if(NP[i].fi[3] == forClientSocketfd && NP[i].fi[2] == -1){
            return i;
		}
    }
	return -1;
}

int pipe_to_or_from_other(int indicator){

	// -1, error
	//  0, no need to pipe to other
	//  1, pipe to other
	
	string tempString;
	threeInt tempThreeInt;
	twoInt tempTwoInt;
	int tempInt;
	bool have_piped_to_other = false;

	for(int j = 0; j < WC[indicator].size(); j++){
		tempString = WC[indicator][j];
		if(tempString[0] == '<'){	// receive
			// erase the '<'
			tempString.erase(tempString.begin());
			if(!is_integer(tempString)){
				continue;
			}

			if(userInfo[stoi(tempString)-1].socketfd == -1){
				cout<<"*** Error: user #"<<tempString<<" does not exist yet. ***"<<endl;
				return -1;
			}
			for(int i = 0; ; i++){
				if(i == userPipeList.size()){
					cout<<"*** Error: the pipe #"<<tempString<<"->#"<<clientNumber+1<<" does not exist yet. ***"<<endl;
					return -1;
				}
				// find the corresponding pipe
				if(userInfo[stoi(tempString)-1].socketfd == userPipeList[i].thi[0] && forClientSocketfd == userPipeList[i].thi[1]){
					tempInt = userPipeList[i].thi[2];
					userPipeList.erase(userPipeList.begin() + i);
					while(i < userPipeList.size()){
						if(userPipeList[i].thi[2] > tempInt)
							userPipeList[i].thi[2]--;
						i++;
					}
					dup2(userPipe[tempInt].ti[0], 0);
					close(userPipe[tempInt].ti[0]);
					userPipe.erase(userPipe.begin() + tempInt);
					break;
				}
			}
			// -------broadcast pipe have received.--------
			char pipe_notification[500] = {}, charArrayTemp[4];
			strcat(pipe_notification, "*** ");
			// ---------------------receiver name-------------------------
			for(int i = 0; i < MAX_CLIENT_NUMBER; i++){	
				if(userInfo[i].socketfd == forClientSocketfd){
					strcat(pipe_notification, userInfo[i].nickname);
					strcat(pipe_notification, " (#");
					sprintf(charArrayTemp, "%d", i+1);
					strcat(pipe_notification, charArrayTemp);
					break;
				}
			}
			// -----------------------sender name---------------------
			strcat(pipe_notification, ") just received from ");
			strcat(pipe_notification, userInfo[stoi(tempString)-1].nickname);
			strcat(pipe_notification, " (#");
			sprintf(charArrayTemp, "%d", stoi(tempString));
			strcat(pipe_notification, charArrayTemp);
			strcat(pipe_notification, ") by '");
			// -------------------------command-----------------------
			strcat(pipe_notification, oneLine.c_str());
			strcat(pipe_notification, "' ***\n");
			broadcast(pipe_notification);
			WC[indicator].erase(WC[indicator].begin()+j);
			j--;
			have_piped_to_other = true;
		}
	}
	for(int j = 0; j < WC[indicator].size(); j++){
		tempString = WC[indicator][j];
		if(tempString[0] == '>'){	// send

			// erase the '>'
			tempString.erase(tempString.begin());
			if(!is_integer(tempString)){
				continue;
			}

			tempThreeInt.thi[0] = forClientSocketfd;
			tempThreeInt.thi[1] = userInfo[stoi(tempString)-1].socketfd;
			tempThreeInt.thi[2] = userPipe.size();
			if(tempThreeInt.thi[1] == -1){
				cout<<"*** Error: user #"<<tempString<<" does not exist yet. ***"<<endl;
				return -1;
			}
			for(int i = 0; i < userPipeList.size(); i++){
				if(forClientSocketfd == userPipeList[i].thi[0] && userPipeList[i].thi[1] == tempThreeInt.thi[1]){
					cout<<"*** Error: the pipe #"<<clientNumber+1<<"->#"<<tempString<<" already exists. ***"<<endl;
					return -1;
				}
			}
			userPipeList.push_back(tempThreeInt);
			pipe(tempTwoInt.ti);
			userPipe.push_back(tempTwoInt);
			dup2(tempTwoInt.ti[1], 1);
			dup2(tempTwoInt.ti[1], 2);
			close(tempTwoInt.ti[1]);
			// -------broadcast pipe have created.--------
			char pipe_notification[500] = {}, charArrayTemp[4];
			strcat(pipe_notification, "*** ");
			// -----------------------sender name---------------------
			for(int i = 0; i < MAX_CLIENT_NUMBER; i++){
				if(userInfo[i].socketfd == forClientSocketfd){
					strcat(pipe_notification, userInfo[i].nickname);
					strcat(pipe_notification, " (#");
					sprintf(charArrayTemp, "%d", i+1);
					strcat(pipe_notification, charArrayTemp);
					break;
				}
			}
			strcat(pipe_notification, ") just piped '");
			// -------------------------command-----------------------
			strcat(pipe_notification, oneLine.c_str());
			strcat(pipe_notification, "' to ");
			// ---------------------receiver name-------------------------
			strcat(pipe_notification, userInfo[stoi(tempString)-1].nickname);
			strcat(pipe_notification, " (#");
			sprintf(charArrayTemp, "%d", stoi(tempString));
			strcat(pipe_notification, charArrayTemp);
			strcat(pipe_notification, ") ***\n");
			broadcast(pipe_notification);
			WC[indicator].erase(WC[indicator].begin()+j);
			j--;

			have_piped_to_other = true;
			needToWaitAllChildProcessTerminated = false;
		}
	}
	
	return have_piped_to_other;
}

void execute_the_command(){
    vector<twoInt> pfds;
    struct twoInt pfd;

    int commandNumber = WC.size(), readFromWhichPipe = read_from_which_pipe();
    bool needToUsePipe = commandNumber > 1;

    pid_t pid = 1;
    vector<pid_t> CPP;    // child processes's pids

    for(int i = 0; i < commandNumber; i++){
        if(pid > 0){
			// connect the pipe from other process
            if(i == 0 || i == commandNumber - 1){
                if(pipe_to_or_from_other(i) == -1) return;
            }

			// generate the pipe
            if(needToUsePipe){
                while(pipe(pfd.ti) == -1);
                pfds.push_back(pfd);
            }

            while((pid = fork()) < 0){  // wait for any child that terminated
				waitpid(-1, NULL, WNOHANG);
				usleep(500);
			}
            if(pid == 0){
				// first command
                if(i == 0){
                    if(readFromWhichPipe != -1){
                        dup2(NP[readFromWhichPipe].fi[0], 0);
                    }
                    if(needToUsePipe) dup2(pfds.back().ti[1], 1);
                }

				// last command
                if(i == commandNumber - 1){
                    if(writeToWhichPipe != -1){
                        dup2(NP[writeToWhichPipe].fi[1], 1);
                        bool stderrToPipe = false;
                        for(int j = 0; j < whichCommandOutStderrToPipe.size(); j++){
                            if(whichCommandOutStderrToPipe[j] == i)
                                stderrToPipe = true;
                        }
                        if(stderrToPipe)
                            dup2(NP[writeToWhichPipe].fi[1], 2);
                    }
                    if(needToUsePipe) dup2(pfds[pfds.size() - 2].ti[0], 0);
                    if(WC[i].size() > 2){
                        if(WC[i][WC[i].size() - 2].compare(">") == 0){
                            char *fileName = to_c_str(WC[i][WC[i].size() - 1]);
                            int rdout = open(fileName, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                            dup2(rdout, 1);
	                        close(rdout);
                            WC[i].pop_back();
	                        WC[i].pop_back();
                            delete [] fileName;
                        }
                    }
                }
                if(needToUsePipe && i != 0 && i != commandNumber - 1){
                    dup2(pfds.back().ti[1], 1);
                    dup2(pfds[pfds.size() - 2].ti[0], 0);
                    bool stderrToPipe = false;
                    for(int j = 0; j < whichCommandOutStderrToPipe.size(); j++){
                        if(whichCommandOutStderrToPipe[j] == i)
                            stderrToPipe = true;
                    }
                    if(stderrToPipe)
                        dup2(pfds.back().ti[1], 2);
                }
                int pfdsSize = pfds.size();
                for(int j = 0; j < pfdsSize; j++){
					close(pfds[j].ti[0]);
					close(pfds[j].ti[1]);
				}
                for(int j = 0; j < NP.size(); j++){
                    close(NP[j].fi[0]);
                    close(NP[j].fi[1]);
                }
                //--------------set args and run-----------------
                int argumentNumber = WC[i].size();
				char **args = new char*[argumentNumber+1];
				for(int j = 0; j < argumentNumber; j++)
					args[j] = to_c_str(WC[i][j]);
				args[argumentNumber] = NULL;

				if(execvp(args[0], args) < 0){
                    dup2(forClientSocketfd, 1);
					cout<<"Unknown command: ["<<WC[i][0]<<"]."<<endl;
                    exit(2);
                }
            }
            if(needToUsePipe){
				if(i > 1){
					close(pfds[i-2].ti[0]);
					close(pfds[i-2].ti[1]);
				}
			}
            CPP.push_back(pid);
        }
    }
    if(needToUsePipe){
		close(pfds.back().ti[0]);
		close(pfds.back().ti[1]);
		pfds.pop_back();
		close(pfds.back().ti[0]);
		close(pfds.back().ti[1]);
	}
    if(readFromWhichPipe != -1){
        close(NP[readFromWhichPipe].fi[0]);
        close(NP[readFromWhichPipe].fi[1]);
        NP.erase(NP.begin() + readFromWhichPipe);
	}
    if(needToWaitAllChildProcessTerminated){
        for(int i = CPP.size() - 1; i > -1; i--){
		    waitpid(CPP[i], NULL, 0);
        }
	}
    else{
        for(int i = 0; i < CPP.size(); i++){
            SRCPP.push_back(CPP[i]);
        }
    }
    int sta;
	for(int i = 0; i < SRCPP.size(); i++){
		waitpid(SRCPP[i], &sta, WNOHANG);
        if(WIFEXITED(sta)){
            SRCPP.erase(SRCPP.begin() + i);
            i--;
        }
	}
}
void work(){
    char *cp1, *cp2;
    char ca1[1100], ca2[10];
    
    struct sockaddr_in clientInfo;
    int tempInt, addrlen = sizeof(clientInfo);

    struct timeval tv, temptv;
	tv.tv_sec = 3000;
	tv.tv_usec = 0;

    fd_set readfds, tempfds;
    FD_ZERO(&readfds);
    FD_ZERO(&tempfds);
    FD_SET(serverSocketfd, &readfds);

    while(1){
        temptv = tv;
        memcpy(&tempfds, &readfds, sizeof(readfds));
        if((tempInt = select(getdtablesize(), &tempfds, NULL, NULL, &temptv)) == -1){
			cout<<"select error"<<endl;
		}
		if(tempInt == 0){
			cout<<"Time out, closing the server."<<endl;
			break;
		}
        // There is a user want to login or type in something.
        if(FD_ISSET(serverSocketfd, &tempfds)){

            if((forClientSocketfd = accept(serverSocketfd, (struct sockaddr*) &clientInfo, (unsigned int*) &addrlen)) < 0){
		        cout<<"Failed to accept."<<endl;
			}
			else{
				cout<<forClientSocketfd<<" entered!"<<endl;
			}

			// make select listen for more socket
			FD_SET(forClientSocketfd, &readfds);

			// ---------pick the valid slut---------
			for(clientNumber = 0; clientNumber < MAX_CLIENT_NUMBER && userInfo[clientNumber].socketfd != -1; clientNumber++);
			
			// client socket file descriptor
			userInfo[clientNumber].socketfd = forClientSocketfd;

			// name
			memset(userInfo[clientNumber].nickname, 0, 50);
			strcat(userInfo[clientNumber].nickname, "(no name)");

			// IP and port, stuck together
			memset(userInfo[clientNumber].IPandPort, 0, 50);
			memset(ca1, 0, 1100);
			strcat(ca1, inet_ntoa(clientInfo.sin_addr));
			strcat(userInfo[clientNumber].IPandPort, ca1);
			strcat(userInfo[clientNumber].IPandPort, "/");
			memset(ca1, 0, 1100);
			sprintf(ca1, "%d",(int)ntohs((int) clientInfo.sin_port));
			strcat(userInfo[clientNumber].IPandPort, ca1);

			// environment variable
			userInfo[clientNumber].path = "bin:.";

			// print welcome message.
            write(forClientSocketfd, welcome_message, strlen(welcome_message));

			// --------broadcast login notification.--------
			memset(ca1, 0, 1100);
			strcat(ca1, "*** User '(no name)' entered from CGILAB/511");
			//strcat(ca1, userInfo[clientNumber].IPandPort);
			strcat(ca1, ". ***\n");

			broadcast(ca1);

			// -------print prompt message.-------
			write(forClientSocketfd, "% ", 2);
            continue;
        }
        // -------Examine if there is a user enter the command.-------
        for(clientNumber = 0; clientNumber < MAX_CLIENT_NUMBER; clientNumber++){

			// user has left.
			if(userInfo[clientNumber].socketfd == -1){
				continue;
			}

            if(FD_ISSET(userInfo[clientNumber].socketfd, &tempfds)){
                forClientSocketfd = userInfo[clientNumber].socketfd;

				// get user's input
                dup2(forClientSocketfd, 0);
                dup2(forClientSocketfd, 2);
				// for debug purpose, dup it later
				//dup2(forClientSocketfd, 1);
				
				// reset PATH
				cp2 = to_c_str(userInfo[clientNumber].path);
				setenv("PATH", cp2, 1);
				delete [] cp2;

                if(read_one_line())
                    continue;
                one_line_have_been_entered();
                
				// print out logs
                cout<<"From "<<forClientSocketfd<<":"<<endl;
                printWC();
                //printNP();

				// change stdin to client socket fd
                dup2(forClientSocketfd, 1);
				
                if(WC[0][0].compare("setenv") == 0){
			        cp1 = to_c_str(WC[0][1]);
			        cp2 = to_c_str(WC[0][2]);
			        setenv(cp1, cp2, 1);
			        delete [] cp1;
					delete [] cp2;
    		        if(WC[0][1].compare("PATH") == 0){
						userInfo[clientNumber].path = WC[0][2];
					}
				}
				else if(WC[0][0].compare("printenv") == 0){
    		        memset(ca1, 0, 1100);
					cp1 = to_c_str(WC[0][1]);
					strcpy(ca1, getenv(cp1));
					strcat(ca1, "\n");
					write(forClientSocketfd, ca1, strlen(ca1));
					delete [] cp1;
				}
				else if(WC[0][0].compare("exit") == 0){

					// clear all the pipes related to the client
      		        for(int i = 0; i < NP.size(); i++){
                        if(NP[i].fi[3] == forClientSocketfd){
       		                close(NP[i].fi[0]);
       		                close(NP[i].fi[1]);
                            NP.erase(NP.begin() + i);
                            i--;
                        }
       		        }
                    for(int i = 0; i < userPipeList.size(); i++){
						if(userPipeList[i].thi[0] == forClientSocketfd || userPipeList[i].thi[1] == forClientSocketfd){
							tempInt = userPipeList[i].thi[2];
							userPipeList.erase(userPipeList.begin() + i);
							close(userPipe[tempInt].ti[0]);
                            close(userPipe[tempInt].ti[1]);
							userPipe.erase(userPipe.begin() + tempInt);
						}
					}

                    // construct the left message
					memset(ca1, 0, 1100);
					strcat(ca1, "*** User '");
					strcat(ca1, userInfo[clientNumber].nickname);
					strcat(ca1, "' left. ***\n");
					broadcast(ca1);

					// reset the userInfo
					userInfo[clientNumber].socketfd = -1;
					userInfo[clientNumber].path = "bin:.";

                    FD_CLR(forClientSocketfd, &readfds);
            		close(forClientSocketfd);
				}
                else if(WC[0][0].compare("who") == 0){

					memset(ca1, 0, 1100);
					strcpy(ca1, who_row);

					for(int i = 0; i < MAX_CLIENT_NUMBER; i++){
						if(userInfo[i].socketfd != -1){
							sprintf(ca2, "%d", i+1);
							strcat(ca1, ca2);		// 0, 1, 2 ...
							strcat(ca1, "	");						// tab
							strcat(ca1, userInfo[i].nickname);	// nickname

							//strcat(ca1, "	CGILAB/511");			// tab
							strcat(ca1, "	");
							strcat(ca1, userInfo[i].IPandPort);	// IP/port

							strcat(ca1, "	");						// tab
							if(forClientSocketfd == userInfo[i].socketfd)
								strcat(ca1, "<-me");				// <-me
							strcat(ca1, "\n");					// end of line
						}
					}
					write(forClientSocketfd, ca1, strlen(ca1));
                }
                else if(WC[0][0].compare("tell") == 0){
					tempInt = -1;
					if(is_integer(WC[0][1]))
						tempInt = userInfo[stoi(WC[0][1]) - 1].socketfd;
					if(tempInt == -1){
						cout<<"*** Error: user #"<<WC[0][1]<<" does not exist yet. ***"<<endl;
					}
					else{

						// construct message
						memset(ca1, 0, 1100);
						strcat(ca1, "*** ");
						strcat(ca1, userInfo[clientNumber].nickname);
						strcat(ca1, " told you ***: ");
						for(int i = 2; i < WC[0].size(); i++){
							strcat(ca1, WC[0][i].c_str());
							if(i != WC[0].size()-1) strcat(ca1, " ");
							else strcat(ca1, "\n");
						}
						write(tempInt, ca1, strlen(ca1));
					}
				}
				else if(WC[0][0].compare("yell") == 0){
					memset(ca1, 0, 1100);
					strcat(ca1, "*** ");
					strcat(ca1, userInfo[clientNumber].nickname);
					strcat(ca1, " yelled ***: ");
					for(int i = 1; i < WC[0].size(); i++){
						strcat(ca1, WC[0][i].c_str());
						if(i != WC[0].size()-1) strcat(ca1, " ");
						else strcat(ca1, "\n");
					}
					broadcast(ca1);
				}
				else if(WC[0][0].compare("name") == 0){
					for(tempInt = 0; tempInt < MAX_CLIENT_NUMBER; tempInt++){
						if(userInfo[tempInt].socketfd != -1 && strcmp(WC[0][1].c_str(), userInfo[tempInt].nickname) == 0){
							break;
						}
					}
					if(tempInt == MAX_CLIENT_NUMBER){
						strcpy(userInfo[clientNumber].nickname, WC[0][1].c_str());
						memset(ca1, 0, 1100);
						// *** User from <IP>/<port> is named '<newname>'. ***

						//strcat(ca1, "*** User from CGILAB/511");
						strcat(ca1, "*** User from ");
						strcat(ca1, userInfo[clientNumber].IPandPort);

						strcat(ca1, " is named '");
						strcat(ca1, WC[0][1].c_str());
						strcat(ca1, "'. ***\n");
						broadcast(ca1);
					}
					else{
						cout<<"*** User '"<<WC[0][1]<<"' already exists. ***"<<endl;
					}
				}
                else{
       		        execute_the_command();
                }

				// send the prompt symbol
                if(userInfo[clientNumber].socketfd != -1){
					write(forClientSocketfd, "% ", 2);
				}

				dup2(storedStd[0], 0);
        		dup2(storedStd[1], 1);
        		dup2(storedStd[2], 2);
    		}
        }
    }
    return;
}

int main(int argc, char **argv, char **envp){
    //clearenv();
    setenv("PATH", "bin:.", 1);

    storedStd[0] = dup(0);
    storedStd[1] = dup(1);
    storedStd[2] = dup(2);

    serverSocketfd = 0, forClientSocketfd = 0;
	if ((serverSocketfd = socket(AF_INET , SOCK_STREAM , 0)) == -1)
        cout<<"Failed to create socket."<<endl;

    unsigned int port;
	sscanf(argv[1], "%u", &port);

	struct sockaddr_in serverInfo;
	memset(&serverInfo, 0, sizeof(serverInfo));
    serverInfo.sin_family = AF_INET;
    serverInfo.sin_addr.s_addr = INADDR_ANY;
    serverInfo.sin_port = htons(port);

    if(bind(serverSocketfd, (struct sockaddr *) &serverInfo, sizeof(serverInfo)) < 0){
		cout<<"Failed to bind."<<endl;
		exit(-1);
	}

    if(listen(serverSocketfd, MAX_CLIENT_NUMBER + 1) < 0){
		cout<<"Failed to listen."<<endl;
		exit(-1);
	}
	for(int i = 0; i < MAX_CLIENT_NUMBER; i++){
		userInfo[i].socketfd = -1;
	}

	work();
	return 0;
}
