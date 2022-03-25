#include <stdio.h>
#include "net/sock/udp.h"
#include <string.h>

#include "net/af.h"
#include "net/protnum.h"
#include "net/ipv6/addr.h"
#include "shell.h"
#include "thread.h"
#include "xtimer.h"

struct job
{
    //Stores the boundarys of the area which the robot needs to search
    int sx;
    int sy;
    int ex;
    int ey;
    //Stores a seperate starting cordinate for if a job is being picked up 
    //again by another robot, will always be updated if robot is lost as a way of keeping 
    //track of how much of the job has been done
    int rdox;
    int rdoy;
    //assigns a bool value to determine if a location has been left 
    //incomplete 
    bool unfinished;
    //Stores the number of squares which have yet to be searched
    int tilesLeft;
    //Allows for the controller to determine if a robot has run out of energy 
    //when it gets lost
    bool lowEnergy;
};


//This struct will hold the position of a mine 
typedef struct position
{
    int x;
    int y;
}position_s;


//LINKED LIST

//container holding positions
typedef struct node {
    position_s pos;
    struct node * next;
} node_t;

node_t * foundSurvivors;

//Adding to linked list
void push(node_t * head, position_s pos) 
{
    node_t * new_node;
    new_node = (node_t*)malloc(sizeof(node_t));

    new_node->pos = pos;
    new_node->next = head;
    head = new_node;
}

bool existInList(node_t * head, position_s pos) 
{
    node_t * current = head;

    while (current != NULL) 
    {
        if (current->pos.x == pos.x && current->pos.y == pos.y) 
        {
            return true;
        }
        current = current->next;
    }
    return false;
}

void printList(node_t* head)
{
    node_t* current = head;

    while (current != NULL) 
    {
        printf("\nSurvivor @<%d,%d>", current->pos.x, current->pos.y);
        current = current->next;
    }
}

//..

//Parameters for displaying IPV6 configuration
extern int _gnrc_netif_config(int argc, char** argv);
uint8_t buf[16];

char *jobFlag = "j";
char *warningFlag = "b";
//For checking if a suvivor has been found
char *suvivorFlag = "s";
char *resumeFlag = "r";

//A boolean which is used to be able to tell if a there is currently a robot traveling 
//to a job 
bool busyTraveling = false;


//Holds the robot addresses
char robot_addresses[MAX_ROBOTS][IPV6_ADDR_MAX_STR_LEN];
bool robot_functional[MAX_ROBOTS];

//Holds the messages given by the cmd user for particular robots
char messages[MAX_ROBOTS][IPV6_ADDR_MAX_STR_LEN];
char warningMessages[MAX_ROBOTS][IPV6_ADDR_MAX_STR_LEN];

//A number which is uded to determine how many warningMessages need 
//to be sent out before a robot performs its job 
int numWarnings = 0;

//Arrays for each of the X and Y cordinates of the bomb location 
position_s warningMessage[NUM_COLUMNS * NUM_LINES];

//Number of Robots begining
int num_robots = 0;
//Number of robots currently active
int num_robots_active = 0;
//Stores the number of suvivors comunicated
int num_suvivors = 0;
//Keeps track of all the robot energy amounts
int amount_energy[MAX_ROBOTS];
//Stores the number of starting / backup jobs
int starting_jobs = 0;
int odd_jobs = 0;

//An array and index into an array for storing the ID of backup jobs waiting to 
//be assigned
int backupJobID[MAX_ROBOTS];

//Goes through a loop of setting all values in the backupJobID array to be -1

int backupJobIndex = 0;

//Sets the sizes of the map to be the size as determined by the Makefile
int xSize = NUM_COLUMNS;
int ySize = NUM_LINES;
//Creates the 2D array representaion for the map which the controller builds
int map[NUM_COLUMNS][NUM_LINES];


//This is the list of stacks for each of the robot threads
char stack[MAX_ROBOTS][THREAD_STACKSIZE_MAIN];

//Possible Commands
int BotCommandsCount = 10;
char* BotCommands[10] = {
    "up", "down", "left", "right", "getSta",
    "runBound", "runPower", "runMines", "runRescue", "runJob"
};


struct job joblist[MAX_ROBOTS]; //Used to store the jobs assiged for the robots

//A methoid used to be able to detemine which the biggest area is that needs to be searched 
//after another robot has blown up or run out of energy, takes in the id of the robot 
//who has finished their job or who is on backupJobID 
bool assignBackup(int id){
    //Used to be able to create a backup job
    char createBackupJob[128];
    //First assigns the passed in ID as the used_id
    int used_id = id;
    //checks if there are any backup jobs from an odd number of robots that need to be assigned, takes priority
    //for this as this robot would have full fuel
    if (odd_jobs == 1){
         //gets the last id (Corresponding to the backup job set)
         used_id = num_robots - 1;
         //Removes the odd job so that it is not used again
         odd_jobs = 0;
    }
    //If the passed in ID was -1 (Meaning that the methiod was called from a robot blowing up
    //or a robot running out of fuel, and no odd_jobs have replaced it) then attempt to use a 
    //id from the list of backupJobID
    else if(used_id == -1){
        //Checks if there are any backup jobs avalilble
        if(backupJobID[backupJobIndex] != -1){
            //assign an ID from the list of backupJobID's 
            used_id = backupJobID[backupJobIndex];
            //Remove the ID from the list by changing it to -1 
            //and decrementing the value of the backupJobIndex 
            backupJobID[backupJobIndex] = -1;
            backupJobIndex--;
		}
        //Otherwise there are no availible backup_workers or odd_jobs to do the request 
        else {
            return false;  
		}      
	}
     
     //Is used to be able to find the highestRemainingID
     int highestRemainingID = -1;
     int highestRemaining = -1;
     //Goes through each of the jobs in the array of 
     for(int i = 0; i < num_robots; i++){
        //looks to find if the index of the job with the highestRemaining 
        //number of tiles which need to be completed
        if((joblist[i].unfinished == true) && (joblist[i].tilesLeft > highestRemaining)){
            //Assigns the highestRemainingID
            highestRemainingID = i;
            //Sets the highestRemaining to be the new highest
            highestRemaining = joblist[i].tilesLeft;
		}
	 }
     //If no id was found, then return false to let know that 
     //there was no location to be able to assign, so no job was saved 
     //Put the robot ID into the array of backup jobs to be called first next time
     //Also checks if the number of tiles is negitive meaning its already been 
     //completed as much as it can be (e.g avoiding a mine causes an area to not 
     //be possible to completed)
     if((highestRemainingID == -1)||(highestRemaining <= 1)){
        backupJobIndex++;
        puts("\nNo backup Jobs avalilble\n");
        //Stores the ID into the backup jobs array 
        backupJobID[backupJobIndex] = used_id;
        return false;

	 }
     //Otherwise assign the job to the id of the given robot
     else {
        //Is used to be able to store the required job to assign a robot
        struct job jb;
        //Reads all the values from the unfinished job to the given job ID's
        jb.sx = joblist[highestRemainingID].sx;
        jb.sy = joblist[highestRemainingID].sy;
        jb.ex = joblist[highestRemainingID].ex;
        jb.ey = joblist[highestRemainingID].ey;
        jb.tilesLeft = joblist[highestRemainingID].tilesLeft;
        jb.unfinished = false;
        //Sets unfinished for the original job to false so it is not picked up again
        joblist[highestRemainingID].unfinished = false;
        //Reads the carry on starting cordinates 
        jb.rdox = joblist[highestRemainingID].rdox;
        jb.rdoy = joblist[highestRemainingID].rdoy;
        //Assigns the new job to the jobs list at the index
        joblist[used_id] = jb;
        //Sends out the message to the robot through reading the sx, sy, 
        //ex, ey, rdox, rdoy and sending to the used_id chosen 
        sprintf(createBackupJob, "%d,%s,%d,%d,%d,%d,%d,%d,%d,", used_id, resumeFlag, jb.sx, jb.sy, jb.ex, jb.ey, jb.rdox, jb.rdoy, jb.tilesLeft);
        strcpy(messages[used_id], createBackupJob);
        puts("robot re-assigned\n");
        printf("value of job: %s\n", createBackupJob);
        printf("Robot ID %d assigned the following: startx = %d, starty = %d, endx = %d, endy = %d, resumex = %d, resumey = %d, tiles given = %d\n", used_id, jb.sx, jb.sy, jb.ex, jb.ey, jb.rdox, jb.rdoy, jb.tilesLeft);
        //If the odd_job was used rather than then given ID
        //then return false to the one which called 
        //the method
        if(used_id != id){
           backupJobIndex++;
            //Stores the ID into the backup jobs array 
            backupJobID[backupJobIndex] = id;
            return true;
		}
        //otherwise return true to let know job has been assigned to the one which called 
        //the method
        return true;
	 }

}


//Is a methoid which allows for the map to be split up between the robots as evenly
//as possible, will store the assigned values for the jobs in an array
void splitMap(void)
{
    //Used to be able to create a job
    char createJob[128];
    //Used to be able to create a warning
    char createWarning[128];
  //Stores the name for the job message
  //char job[3] = "job";
  //stores an aray of char* for assigning jobs 
  //char*  jobs[MAX_ROBOTS];

  //Goes through a loop of setting all values in the backupJobID array to be -1
  //for telling if there are ids for robots wanting to be assigned jobs
  for (int i = 0; i <= MAX_ROBOTS; i++){
    backupJobID[i] = -1;
  }
  //Stores the number of robots being assigned jobs at the begining
  starting_jobs = 0;
  odd_jobs = 0;
  //Stores the area values for how each area will be split up
  int x_split = 0;
  int y_split = 0;
  int start_x = 0;
  int start_y = 0;
  //Is used to be able to store the required job to assign a robot
  struct job jb;
  //In the case that there is only 1 robot, assign the entire map to the job
  //slot 1
  if (num_robots == 1){
    //To keep track of what areas are assigned from the controller side. in order 
    //to reassign areas if other robots blow up or run out of energy  
    jb.sx = 0;
    jb.sy = 0;
    jb.ex = xSize - 1;
    jb.ey = ySize - 1;
    jb.unfinished = false;
    //Calculates the number of tiles which needs to be searched 
    jb.tilesLeft = xSize * ySize;
    jb.lowEnergy = false;
    joblist[0] = jb;
    starting_jobs = num_robots;
    puts("1 robot assigned\n");
    sprintf(createJob, "%d,%s,%d,%d,%d,%d,%d,", 0, jobFlag, start_x, start_y, jb.ex, jb.ey, jb.tilesLeft);
    strcpy(messages[0], createJob);
    printf("value of job: %s\n", createJob);
    printf("Robot ID %d assigned the following: startx = %d, starty = %d, endx = %d, endy = %d, tiles given = %d\n", 0, jb.sx, jb.sy, jb.ex, jb.ey, jb.tilesLeft);
  }
  else {
    //Otherwise if the number of robots is not even, then leave the last robot
    //to not have a job until needed at a later point
    if (num_robots % 2 == 1){
      starting_jobs = num_robots - 1;
      odd_jobs = 1;
    }
    //If the number of robots is even, then split the map up between all robots
    else if (num_robots % 2 == 0){
      starting_jobs = num_robots;
    }
    printf("Robots being assigned jobs: starting = %d, backup = %d\n", starting_jobs, odd_jobs);
    printf("Height and Width of Map: X Size = %d, Y Size = %d\n", xSize, ySize);
    //Is used to be able to set the assigned starting x / y
    start_x = 0;
    start_y = 0;
    int end_x = 0;
    int end_y = 0;

    //Goes through and splits up the map for the given starting jobs

    //Divides the width by the number of jobs there is, checks which dimentions to
    //divide by in order to prevent the number from being greater than the x size

    if (xSize < (starting_jobs / 2)) {
      x_split = 2;
      y_split = starting_jobs / 2;
    }
    //Otherwise there is no need to determine the way to split up, divide by the
    //x side by half the starting_jobs and have the y side divided by 2
    else {
      y_split = 2;
      x_split = starting_jobs / 2;
    }
    //Finds the area for each side of the map to be given to the different robots,
    //Divides by the split to determine this
    //Goes through a loop of each of the split values to get the even share of the
    //map assigned
    //Sets up the count for indexing the robbot jobs list
    int rID = 0;
    //For each of the different y_split sections
    for (int i = 1; i <= y_split; i++) {
        if(i == y_split){
            //Calulates the remainder that needs to be added
            end_y = end_y + (ySize / y_split) + (ySize % y_split);
            //printf("Adding Y remainder\n");
        }
        //Otherwise add the normal amount to the total
        else {
            end_y = end_y + (ySize / y_split);
            //printf("%d,y\n", end_y);
        }
      //For each of the different x_split sections
      for (int j = 1; j <= x_split; j++) {
        //First checks if the value is at the end of the split sections to know if
        //the remainder needs to be added to the last section given to the robot
        if (j == x_split){
          //Calulates the remainder that needs to be added
          end_x = end_x + (xSize / x_split) + (xSize % x_split);
          //printf("Adding X remainder\n");
        }
        //Otherwise add the normal amount to the total
        else {
          end_x = end_x + (xSize / x_split);
        }
        //Subtracts 1 from each in order to prevent the other robot jobs from overlaping 
        //beggining locations
        //Assigns the job to the list
        jb.sx = start_x;
        jb.sy = start_y;
        //Subtracts 1 from each in order to prevent the other robot jobs from overlaping 
        //beggining locations
        jb.ex = end_x - 1;
        jb.ey = end_y - 1;
        jb.lowEnergy = false;
        jb.unfinished = false;
        jb.tilesLeft = (end_x - start_x) * (end_y - start_y);
        //Assigns the last end_x and end_y to be the starting positions
        joblist[rID] = jb;
        //Checks first if there is a warning which needs to be sent out 
        if (numWarnings != 0){
            //Goes through a loop of sending out each of the different warnings 
            for(int i = 1; i <= numWarnings; i++){
              //Reads the X and Y codinates to create the message
              sprintf(createWarning, "%d,%s,%d,%d,", rID, warningFlag, warningMessage[i].x, warningMessage[i].y);
              strcpy(messages[rID], createWarning);
              //Waits for the network to send and for the warning to be interpreted 
              sleep(1);
			}
		}
        //Sends out the message to the robot through reading the sx, sy
        //ex, ey of the joblist[rID]
        sprintf(createJob, "%d,%s,%d,%d,%d,%d,%d,", rID, jobFlag, start_x, start_y, jb.ex, jb.ey, jb.tilesLeft);
        strcpy(messages[rID], createJob);
        printf("value of job: %s\n", createJob);
        //Sets the value of the busyTraveling bool to be true 
        //in order to prevent the next job from being assigned until the the robot has reached its starting point 
        busyTraveling = true;
        //Prints out the different values assigned for the job in order to
        //debug / view them
        printf("Robot ID %d assigned the following: startx = %d, starty = %d, endx = %d, endy = %d, tiles given = %d\n", rID, jb.sx, jb.sy, jb.ex, jb.ey, jb.tilesLeft);
        //Increments the value of the robot ID to point to the next job slot
        rID++;
        //Updates the x starting cordinate
        start_x = start_x + (xSize / x_split);
        //While the busyTraveling is set to be true 
        while(busyTraveling){
            sleep(1);  
            puts("busy Waiting");
		}
        //wait in order to prevent a new job from being assigned 
      }
      //Updates the y starting cordinate to point to the next start_y
      start_y = start_y + (ySize / y_split);
      //puts the x value back to the start position
      start_x = 0;
      //Puts the x value back to the starting value
      end_x = 0;
    }
  }
}



//A methoid which allows for the map to be initialised to have all blank
//not yet found 0's
void initialiseMap(void)
{
  //Goes through a loop of updating all the values in the 2d array
  //For each of the x cordinates
  for (int i = 1; i <= xSize; ++i){
    //For each of the y cordinates
    for (int j = 1; j <= ySize; ++j) {
      //Sets the value of the cell to be 0 representing a non explored
      //square
      map[i][j] = 0;
    }
  }
}

//A methoid which allows for discovered tiles to be recorded once they are discovered
//bombs (2), suvivors (3), or searched blank areas (4)
void addToMap(int tile_type, int tile_x, int tile_y)
{
  printf("Adding to Map Location: Tile Type:%d, X:%d, Y:%d \n\n", tile_type, tile_x, tile_y);
  //Indexes the point where the tile is being pointed at and sets the value
  //of the location to be that tile type
  map[tile_x][tile_y] = tile_type;
  //If the tile is a suvivor then add one to the number of suvivors
  if (tile_type == 3){
    position_s cur;
    cur.x = tile_x;
    cur.y = tile_y;
    if (!existInList(foundSurvivors, cur)) 
    {
        push(foundSurvivors, cur);
        num_suvivors = num_suvivors + 1;
    }
    else 
    {
        puts("Survivor has already been found\n\n");
    }
  }
  //If the tile is a bomb then send out a warning message
  else if (tile_type == 2){

  }
  //Otherwise, the tile is blank then the location is not of interest and has been
  //searched already
}

//This thread is responsible for handling each robot added to the program
//It sends, recieves messages from said ROBOT ID and also constantly checks
//the cmd messages array for possible jobs
void* robot_thread_handler(void* args)
{
    //Stores a boolean value for determining which direction the robot is expecting to go 
    bool plusX = true;
    bool plusY = false;
    //Stores the last cordinates used to determine which way the robot has moved 
    int prevX = -1;
    //int prevY = -1;

    //read ID
    int id = *((int*)args);
    printf("\ncontroller thread handler for robot %d waiting for commands from console\n", id);

    //Create local socket for particular robot
    sock_udp_ep_t robotAD = SOCK_IPV6_EP_ANY;
    robotAD.port = 10000 + id;

    //Create UDP sock for this robot
    sock_udp_t robotSock;
    if (sock_udp_create(&robotSock, &robotAD, NULL, 0) < 0)
    {
        puts("\nError creating UDP sock\n");
        return NULL;
    }

    //Setup Remote
    sock_udp_ep_t remote = { .family = AF_INET6 };
    //Convert the string robot address
    if (ipv6_addr_from_str((ipv6_addr_t*)&remote.addr.ipv6, robot_addresses[id]) == NULL)
    {
        puts("\nCannot convert server address\n");
        sock_udp_close(&robotSock);
        return NULL;
    }
    remote.port = 10000 + id;

    ipv6_addr_set_all_nodes_multicast((ipv6_addr_t*)&remote.addr.ipv6, IPV6_ADDR_MCAST_SCP_LINK_LOCAL);

    ssize_t res;

    //This flag is responsible for breaking the message recieving loop
    char closeFlag[] = "e";
    //Used to see if a robot has started a job
    char startedFlag[] = "i";
    //Used to see if a robot has low energy
    char lowEnergyFlag[] = "l";

    //This takes in the messages being recieved
    char bufR[128];
    char createWarning[128];

    //This flag is for the local code to know whether or not the job is done
    //and the message array is open to clear for another job
    bool open = true;

    //loop forever
    while (1) 
    {
        //If a job exists in the message array
        if (strlen(messages[id]) == 0) 
        {
            xtimer_sleep(1);
            continue;
        }
        else 
        {
            //Set robot to busy
            open = false;
            printf("Num Warnings being sent: %d\n", numWarnings);
            //Checks first if there is a warning which needs to be sent out 
            if (numWarnings > 0){
                //Goes through a loop of sending out each of the different warnings 
                for(int i = 0; i < numWarnings; i++){
                  //Reads the X and Y codinates to create the message
                  sprintf(createWarning, "%d,%s,%d,%d,", id, warningFlag, warningMessage[i].x, warningMessage[i].y);
                  strcpy(warningMessages[id], createWarning);
                  if (sock_udp_send(&robotSock, warningMessages[id], strlen(warningMessages[id])+1, &remote) < 0) 
                  {
                      puts("\nError sending message\n");
                      sock_udp_close(&robotSock);
                      xtimer_sleep(1);
                  }
			    }
		    }
            //read feedback of the given command to the console
            printf("will send command [%s] to robot %d (%s)\n", messages[id], id, robot_addresses[id]);
            if (sock_udp_send(&robotSock, messages[id], strlen(messages[id])+1, &remote) < 0) 
            {
                puts("\nError sending message\n");
                sock_udp_close(&robotSock);
                return NULL;
            }
            printf("Sent!\n");
            //Constantly read for messages from the robot until job is done
            while (open == false) 
            {
                //Read if recieve socket
                res = sock_udp_recv(&robotSock, bufR, sizeof(bufR), 10 * US_PER_SEC, NULL);
                //if message exists
                if (res >= 0) 
                {
                    //read and display message
                    bufR[res] = 0;
                    printf("Recieved from robot %d: [%s]\n", id, (char*)bufR);
                    //If "l" is read then set the joblist[id].lowEnergy to be true
                    if(strcmp((char*)bufR, lowEnergyFlag) == 0){
                        printf("Robot %d low on energy\n", id);
                        //Sets the robot to be low on energy 
                        joblist[id].lowEnergy = true;
					}
                    //if 'e' is seen then close the reciever and open the message array
                    else if (strcmp((char*)bufR, closeFlag) == 0)
                    { 
                        //Sets the busyTraveling to be false
                        busyTraveling = false;
                        open = true;
                        printf("\nRobot %d is open for another command!\n", id);
                        //Runs the assignBackup methoid to try give the backup robot an unfinished
                        //job
                        assignBackup(id);
                    }
                    
                    //If the job does not equal started (sent through 1), then plot the location 
                    else if (strcmp((char*)bufR, startedFlag) != 0)
                    {
                        //Splits the location into the cordinates 
                        char location[128];
                        strcpy(location, (char*)bufR);
                        char* token = strtok(location, ",");
                        int strCount = 0;
                        char* locationXY[4];
                        while (token != NULL)
                        {
                            locationXY[strCount] = token;
                            strCount++;
                            token = strtok(NULL, ",");
                        }
                        //Checks first if there is a token which is equivent to "s"
                        //meaning that a suvivor was found 
                        if(strcmp(locationXY[0], suvivorFlag) == 0){
                            //Adds one to the number of suvivors 
                            num_suvivors = num_suvivors + 1;
                            printf("\nSuvivor Found at X:%d, y:%d! Total Suvivors:%d\n", atoi(locationXY[1]), atoi(locationXY[2]), num_suvivors);
                            //Changes the postion of the following X and Y cordinates 
                            locationXY[0] = locationXY[1];
                            locationXY[1] = locationXY[2];
                            //Plots the location as suvivor tile (3)
                            addToMap(3, atoi(locationXY[0]), atoi(locationXY[1]));
						}
                        else {
                            //Updates the map with the cordinates received 
                            addToMap(4, atoi(locationXY[0]), atoi(locationXY[1]));              
						}
                        //First checks if the job has reached the starting cordinates 
                        //before beginging to remove from the tiles left 
                        if((atoi(locationXY[0]) >= joblist[id].sx) && (atoi(locationXY[1]) >= joblist[id].sy)) {
                            //Sets the busyTraveling to be false
                            busyTraveling = false;
                            //if((atoi(locationXY[0]) >= 10) && (atoi(locationXY[1]) >= 10)) {
                            //Updates the value of the tiles covered at the robot ID 
                            joblist[id].tilesLeft--; 
                            //printf("Tiles Remaining:%d\n", joblist[id].tilesLeft);
                            //Updates the last location to be the latest 
                            joblist[id].rdox = atoi(locationXY[0]);
                            joblist[id].rdoy = atoi(locationXY[1]);
                            //Determines in which way the robot moved since its last move
                            //If Y has increased its location 
                            //Else if X has changed 
                            if(atoi(locationXY[0]) != prevX){
                                //If the current X is equal to the end point
                                if(atoi(locationXY[0]) == joblist[id].ex){
                                    //puts("\nX-, Y+\n");
                                    //Set the plusX bool to be false
                                    plusX = false;
                                    //Changes plusY to be true as next move will be down Y
                                    plusY = true;
								}
                                //else if the current X is equal to the start point
                                else if(atoi(locationXY[0]) == joblist[id].sx){
                                    //puts("\nX+, Y+\n");
                                    //Set the plusX bool to be true
                                    plusX = true;
                                    //Changes plusY to be true as next move will be down Y
                                    plusY = true;
								}  
							}
                            //Else if Y has increased its location 
                            else {
                                //Sets plusY to be false as move has been made 
                                plusY = false;
							}
                            //Update the prevX
                            prevX = atoi(locationXY[0]);
						} 
                    }
                }
                //This timeout is to catch whether or not it can be recieved
                else 
                {
                    if (res == -ETIMEDOUT) 
                    {
                        //Checks the charge of the robot assigned on the job to know if
                        //it is expected to be a bomb which caused the timeout 

                        printf("\nRobot %d has been lost\n", id);
                        //Aims to work out the location where the robot blew up 
                        //from looking at the booleans for plusX and plusY to know 
                        //what the next move will be 
                        joblist[id].unfinished = true;
                        //Decrements the number of robots currently going 
                        num_robots_active--;


                        //First checks if there is a chance that the robot has blown up
                        //Checks this by seeing if the amount of energy is greater than 
                        //what is needed to make a move 
                        if (joblist[id].lowEnergy != true){
                            //Works out to cordinate from the last recorded x / y
                            //and bool values for the predicted movement 
                            int predictedX = 0;
                            int predictedY = 0;
                            //Checks if the robot failed at the stage of the getting to the 
                            //starting point 
                            if((joblist[id].rdox < joblist[id].sx)||(joblist[id].rdoy < joblist[id].sy)){
                                //If X is not at the starting x postion
                                if(joblist[id].rdox < joblist[id].sx){
                                     predictedX = 1;                
								}
                                //Else if the x is at the right position, then need to add to the 
                                //Y cordinate
                                else if(joblist[id].rdoy < joblist[id].sy){
                                    predictedY = 1; 
								}
                                //Adds the last values to the predictedX and predictedY
                                predictedX = predictedX + joblist[id].rdox;
                                predictedY = predictedY + joblist[id].rdoy;
							}
                            //Otherwise if it has already reached the starting point 
                            else {
                                //If the plusY is set, then keep the same x, but increase y
                                if(plusY){
                                    predictedX = joblist[id].rdox;
                                    predictedY = joblist[id].rdoy + 1;
							    }
                                //Otherwise if plusX is set to be true, Increment x
                                else if(plusX){
                                    predictedX = joblist[id].rdox + 1;
                                    predictedY = joblist[id].rdoy;                  
							    }
                                //Otherwise plusX is false so decrement x
                                else {
                                    predictedX = joblist[id].rdox - 1;
                                    predictedY = joblist[id].rdoy;  
							    }
							}
                            //Sets the bomb threat message to be to all 
                            //of the different robot ID's by incrementing the value of the 
                            //numWarnings
                            //Creates a location using the predicted x / y 
                            position_s bombLocation;
                            bombLocation.x = predictedX;
                            bombLocation.y = predictedY;
                            //Put a message to the list in order to have the bomb threat 
                            //be read before sending out a job
                            warningMessage[numWarnings] = bombLocation;
                            numWarnings++;

                            //Adds the bomb to the map for visual representation 
                            addToMap(2, predictedX, predictedY);                            
                        }
                        //Sets the busyTraveling to be false
                        busyTraveling = false;
                        puts("Trying to reassign job");


                        //Calls the assign backup with -1 to say that it is being
                        //called after having blown up / running out of energy
                        bool assigned = assignBackup(-1);
                        //If assigned the print to the console 
                        if (assigned) {
                            puts("Resume job assigned to another robot");
					    }
                        else {
                            puts("No robots avalible to resume job");
                            //checks if there are any other jobs that are in progress 
                            //rather than ending 
                            if(num_robots_active <= 0){
                                //If there are no current jobs in progress, then print the 
                                //number of suvivors found by the end of the search 
                                puts("\n\nFinished Search!\n\n");
                                printList(foundSurvivors);
                                puts("\nFinal Map:\n");
                                puts("\nKey: 4 = searched, 3 = survivor, 2 = bomb 0 = unsearched\n");
                                //Goes through a loop of outputting the map to the console 
                                for(int i = 0; i < xSize; i++){
                                    for(int j = 0; j < ySize; j++){
                                    //Prints without creating a new line 
                                    printf("%d", map[i][j]);
	                                }
                                    //Prints out a \n to move on to the next line 
                                    printf("\n");
                                }
                                int suvivorCount = 0;
                                int bombCount = 0;
                                puts("\n\nBomb Cordinates Predicted:\n\n");
                                //Goes through a loop of outputting the map to the console 
                                for(int i = 0; i < xSize; i++){
                                    for(int j = 0; j < ySize; j++){
                                    //Prints without creating a new line 
                                        if(map[i][j] == 3){
                                            //Adds 1 to the count 
                                            bombCount++;
                                            printf("Bomb %d: X:%d, Y:%d\n", bombCount, i, j);
										}
	                                }
                                }
                                puts("\n\nSurvivors Found:\n\n");
                                //Goes through a loop of outputting the map to the console 
                                for(int i = 0; i < xSize; i++){
                                    for(int j = 0; j < ySize; j++){
                                    //Prints without creating a new line 
                                        if(map[i][j] == 3){
                                            //Adds 1 to the count 
                                            suvivorCount++;
                                            printf("Survivor %d: X:%d, Y:%d\n", suvivorCount, i, j);
										}
	                                }
                                }
                                exit(0);
							}
                            return NULL;

                        }
                    }
                    else
                    {
                        puts("\nError reciving message\n");
                        return NULL;
                        //Sets the busyTraveling to be false
                        busyTraveling = false;
                    }
                }
            }
            //Clear the message array
            //memset(messages[id], 0, sizeof(messages[id]));
        }
    }
}





















//This is the command function when 'send' is called in the cmd
//funcition: send <robotID> <command> then stores message in the
//corresponding robotID message array
extern int sendMessage_cmd(int argc, char *argv[])
{
    //If not used properly
    if (argc < 3 && argc > 4)
    {
        printf("Usage: send <robotID> <command>\n");
        printf("To get list of send commands use <!send>\n");
        return 1;
    }
    else
    {
        //read ID and Command
        int id = atoi((char*)argv[1]);
        char* command = argv[2];
        if (strcmp(command, "j") == 0)
        {
            char comJob[128];
            sprintf(comJob, "%s,%s", (char*)argv[2], (char*)argv[3]);
            //feedback to user what they have entered
            if (strlen(messages[id]) == 0)
            {
                printf("sending command %s %s to robot %d\n", (char*)argv[2], (char*)argv[3], id);
                strcpy(messages[id], comJob);
            }
            else
            {
                printf("robot %d is busy..\n", id);
            }
        }
        else 
        {
            if (argc == 3) 
            {
                bool commandExists = false;
                //Check if command exists
                for (int i = 0; i < BotCommandsCount; i++)
                {
                    if (strcmp(BotCommands[i], command) == 0)
                    {
                        commandExists = true;
                    }
                }
                //If exists add it to messages queue for robot
                if (commandExists == true)
                {
                    //feedback to user what they have entered
                    if (strlen(messages[id]) == 0)
                    {
                        printf("sending command %s to robot %d\n", command, id);
                        strcpy(messages[id], command);
                    }
                    else
                    {
                        printf("robot %d is busy..\n", id);
                    }
                }
                else
                {
                    printf("Command does not Exist! use <!send> to get list of commands\n");
                }
            }
            else 
            {
                printf("Too Many Arguments for that command. Only <j> accepts 4 Arguments\n");
            }
        }
        //This occurs if the message queue for the robot is still in use
        return 0;
    }
}

extern int listMessages_cmd(int argc, char* argv[])
{
    (void)argv;
    if (argc != 1)
    {
        printf("Usage: !send\n");
        return 1;
    }
    else 
    {
        //Basic single moves
        puts("\n-- SINGLE COMMANDS ---\n");
        puts("\n<robotID> up: Moves UP by 1 tile");
        puts("\n<robotID> down: Moves DOWN by 1 tile");
        puts("\n<robotID> left: Moves LEFT by 1 tile");
        puts("\n<robotID> right: Moves RIGHT by 1 tile");
        puts("\n<robotID> getSta: RETURNS the current status of the robot\n");
        puts("\n-- ROUTINES ---\n");
        puts("\n<robotID> j <StartX>,<StartY>,<EndX>,<EndY>: Searches given area");
        puts("\n<robotID> r <StartX>,<StartY>,<EndX>,<EndY>,<ResX>,<ResY>: Resumes a search of a given area");
        puts("\n<robotID> runBound: Runs a routined path to test boundries(DEBUG)");
        puts("\n<robotID> runPower: Runs a routined path to test power behaviour(DEBUG)");
        puts("\n<robotID> runMines: Runs a routined path to test mines behaviour(DEBUG)");
        puts("\n<robotID> runRescue: Runs a routined path to test survivor behaviour(DEBUG)");
    }
    return 0;
}

//Holds commands for CMD shell
static const shell_command_t commands[] =   
{
    {"send", "send command to given robot", sendMessage_cmd},
    {"!send", "list the commands that can be sent", listMessages_cmd},
	{NULL, NULL, NULL}
};

//This is the main method that sets up the IPV6 config
//for the server and creates the threads for all the 
//given bots
int main(void) 
{
    puts("This is the controller");

    // print network addresses. this is useful to confirm the right addresses are being used
    puts("Configured network interfaces:");
    _gnrc_netif_config(0, NULL);

    // configure a local IPv6 address and set port to be used by this server
    sock_udp_t sock;
    sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
    local.port = 10000;
    // create and bind sock to local address
    if (sock_udp_create(&sock, &local, NULL, 0) < 0) {
        puts("Error creating UDP sock");
        return 1;
    }
    // the server is good to go
    printf("server waiting on port %d\n", local.port);

    //Extract the robot address parameters
    char str[999];
    strcpy(str, ROBOT_ADDRESSES);
    char* token = strtok(str, ",");

    //Create a thread for each Robot Address
    int id = 0;
    while (token != NULL)
    {
        strcpy(robot_addresses[id], token);
        thread_create(stack[id],
            sizeof(stack[id]),
            THREAD_PRIORITY_MAIN - 1 - id,
            THREAD_CREATE_STACKTEST,
            robot_thread_handler,
            (void*)&id,
            "control_thread");
        num_robots++;
        printf("%d id created\n", id);
        id++;
        token = strtok(NULL, ",");
    }
    //Assigns the number of robots in action
    num_robots_active = num_robots;
    //Splits up the MAP
    splitMap();

    //Initilize Shell
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(commands, line_buf, SHELL_DEFAULT_BUFSIZE);

	return 0;
}
