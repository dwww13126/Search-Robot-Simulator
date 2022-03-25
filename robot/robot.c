#include <string.h>
#include <stdio.h>
#include "net/af.h"
#include "net/protnum.h"
#include "net/ipv6/addr.h"
#include "net/sock/udp.h"
#include "shell.h"
#include "thread.h"
#include "xtimer.h"

//This struct will hold the position of an object in
//the map grid
struct position
{
    int x;
    int y;
};

//MALLOC
void sendMessage(int argc);
int cost = 0;

//Global Parameters
uint8_t buf[128];
char bufMessage[128];
char stack[THREAD_STACKSIZE_MAIN];
sock_udp_t sock;
sock_udp_ep_t remote;

//To store the alerted mine locations
int alertedMinesListX[NUM_LINES * NUM_COLUMNS];
int alertedMinesListY[NUM_LINES * NUM_COLUMNS];
//Index into the alerted mineList
int alertedMinesListIndex = 0;


//Map Boundary
int boundaryX = NUM_LINES;
int boundaryY = NUM_COLUMNS;

//Map Traits
struct position mineList[MAX_MINES]; //Mine List and Count
int mineCount = 0;

struct position survivorList[NUM_LINES * NUM_COLUMNS]; //survivor List and count
int survivorCount = 0;

int foundSurvivors = 0;

//Robot Location
struct position robotPos;

//Total Energy
int energy = ENERGY * 100;

//For determining how much energy was used
int energyStartingCalculation = ENERGY * 100;

//Given Robot ID
int id = ROBOT_ID;

//Energy costs
int moveCost = 100; //moving
int messageCost = 1; //sending/Receiving Messages

//Functioning Booleans
bool isFunctional = true; //Is bot still functional
bool canMove = true; //Can bot Move (exculding messaging)

//Parameters for displaying current IPV6 configuration
extern int _gnrc_netif_config(int argc, char** argv);

//This method checks the status of the robot's power reserve and changes
//it if it cannot execute commands
void CheckPower(int arg)
{
    char lowPowerMessage[128];
    int newEnergy = energy - arg;
    printf("\n[%d - %d]", energy, arg);
    //Check if bot needs to freeze the ability to move due to not enough power reserve
    if (newEnergy <= 100 && isFunctional)
    {
        canMove = false;
        printf("\nWARNING: Power Low: %d Cannot Move\n", energy);
        puts("Sending low power message");
        sprintf(lowPowerMessage, "l");
        strcpy(bufMessage, lowPowerMessage);
        sendMessage(0);
        printf("\nSuvivors Found: %d , Energy Used: %d\n", foundSurvivors, (energyStartingCalculation - energy));
        //Exits the program
        exit(0);
    }
    else
    {
        energy = newEnergy;
    }
}

//A methoid which checks if a tile is known to be a bomb given the warning
//messages sent through by the controller, returns False if the tile is safe
//to move on, returns True if not safe, takes in the proposed x and y co-ordinates
bool isUnsafe(int proposedX, int proposedY){
    //Checks the proposed values to all the discovered bombs comunicated
    //from the controller
    for(int i = 0; i < alertedMinesListIndex; i++){
        //Checks if both the cordinates to see if the move will
        //cause a mine to be hit
        //printf("Checking (X = %d, Y =%d)\n", proposedX, proposedY);
        if((proposedX == alertedMinesListX[i]) && (proposedY == alertedMinesListY[i])){
            return true;
		}
        //printf("Mine at (X = %d, Y =%d)\n", alertedMinesListX[i], alertedMinesListY[i]);
	}
    //If no matches were found then return False
    return false;
}

//This method checks what is on the tile the robot has just moved on
//It will act accordingly if either a survivor or mine has been landed on
void CheckTile(int arg)
{
    (void)arg;
    bool tileEmpty = true;
    //Check if the tile holds a mine
    for (int i = 0; i < mineCount; i++)
    {
        struct position curMine = mineList[i];
        if (robotPos.x == curMine.x && robotPos.y == curMine.y)
        {
            isFunctional = false;
            printf("\n\nKABOOOOOOM!!!!\n\n");
            printf("\nSuvivors Found: %d , Energy Used: %d\n", foundSurvivors, (energyStartingCalculation - energy));
            tileEmpty = false;
            //Exits the program
            exit(0);
        }
    }
    //Check if the tile holds a survivor
    for (int i = 0; i < survivorCount; i++)
    {
        struct position curSurvivor = survivorList[i];
        if (robotPos.x == curSurvivor.x && robotPos.y == curSurvivor.y)
        {
            printf("\n\nSurvivor found at coordinates (%d, %d)!\n\n", robotPos.x, robotPos.y);
            foundSurvivors++;
            char updateC[128];
            sprintf(updateC, "s,%d,%d", robotPos.x, robotPos.y);
            strcpy(bufMessage, updateC);
            sendMessage(0);
            tileEmpty = false;
        }
    }
    if (tileEmpty == true)
    {
        char updateC[128];
        sprintf(updateC, "%d,%d", robotPos.x, robotPos.y);
        strcpy(bufMessage, updateC);
        sendMessage(0);
    }
}

//This method is the check for when a bot hits a wall
//This stops bots from going gurther that it current position
bool OutOfBounds(int arg, int argv)
{
    int curPos = arg; //Bots Position
    int direction = argv; //What Direction is the Bot moving to
    //Check what is ahead
    if (direction == 0)
    {
        curPos--;

    }
    else if (direction == 1)
    {
        curPos++;
    }
    else if (direction == 2)
    {
        curPos--;
    }
    else
    {
        curPos++;
    }
    //If it is out of bounds send warning
    if ((direction == 0 || direction == 1) && (curPos < 0 || curPos > boundaryX))
    {
        printf("Robot Hit Bounds of Map\n");
        sleep(1);
        return true;
    }
    else if ((direction == 2 || direction == 3) && (curPos < 0 || curPos > boundaryY))
    {
        printf("Robot Hit Bounds of Map\n");
        sleep(1);
        return true;
    }
    return false;
}

//A set of more basic up, down, left and right commands used to avoid mines
//which have been comunicated

bool avoidUp(int arg)
{
    (void)arg;
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove || OutOfBounds(robotPos.y, 0))
    {
        return false;
    }
    CheckPower(moveCost); //Check the state of power
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove)
    {
        return false;
    }
    printf("moving UP [cost: %d]\n", moveCost);
    robotPos.y--; //move towards the request direction (int this case UP)
    CheckTile(0); //Check the tile the bot has landed on
    xtimer_usleep(400000);
    return true;
}

bool avoidDown(int arg)
{
    (void)arg;
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove || OutOfBounds(robotPos.y, 1))
    {
        return false;
    }
    CheckPower(moveCost); //Check the state of power
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove)
    {
        return false;
    }
    printf("moving DOWN [cost: %d]\n", moveCost);
    robotPos.y++;
    CheckTile(0);
    xtimer_usleep(400000);
    return true;
}

bool avoidLeft(int arg)
{
    (void)arg;
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove || OutOfBounds(robotPos.x, 2))
    {
        return false;
    }
    CheckPower(moveCost); //Check the state of power
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove)
    {
        return false;
    }
    printf("moving LEFT [cost: %d]\n", moveCost);
    robotPos.x--;
    CheckTile(0);
    xtimer_usleep(400000);
    return true;
}

bool avoidRight(int arg)
{
    (void)arg;
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove || OutOfBounds(robotPos.x, 3))
    {
        return false;
    }
    CheckPower(moveCost); //Check the state of power
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove)
    {
        return false;
    }
    printf("moving RIGHT [cost: %d]\n", moveCost);
    robotPos.x++;
    CheckTile(0);
    xtimer_usleep(400000);
    return true;
}

//The next few methods are responsible for moving the bot a single tile
//to the given direction **Explainations are in the first method**
bool sUp(int arg)
{
    (void)arg;
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove || OutOfBounds(robotPos.y, 0))
    {
        return false;
    }
    CheckPower(moveCost); //Check the state of power
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove)
    {
        return false;
    }
    //Determines the move to see if it is ok to do given the locations
    //known for where comunicated mines are
    bool move = isUnsafe(robotPos.x, robotPos.y - 1);
    //Try navigate around the mine by first trying to set x to + 1, then
    //y to y - 1, 3 times and then x to - 1, if not possible to go x + 1
    //(When a wall is in the way), then try x - 1 (To go backwards)
    //then y - 1 3 times and x + 1 to get around
    if(move){
        puts("\nattempting to make way around communicated mine\n");
        //First checks if it can get past by moving in y direction 3 times
        if((robotPos.y - 3) <= boundaryY){
            //Checks which way to go around the mine
            if((robotPos.x + 1) <= boundaryX){
                avoidRight(0);
                //Moves along y 3 times
                avoidUp(0);
                avoidUp(0);
                avoidUp(0);
                //Moves back to the positon after the mine
                avoidLeft(0);
	        }
            else if((robotPos.x - 1) >= 0){
                avoidLeft(0);
                //Moves along y 3 times
                avoidUp(0);
                avoidUp(0);
                avoidUp(0);
                //Moves back to the positon after the mine
                avoidRight(0);
		    }
	    }
        //Otherwise attempt to only move 2, check if there is room
        // to move aroundwithout hiting a wall
        else if((robotPos.x + 1) <= boundaryX){
            avoidRight(0);
            //Moves along y 2 times
            avoidUp(0);
            avoidUp(0);
	    }
        //Otherwise is on the final row and there is no reason to move past
        //Send back 0 to let know that the job is finished
	}
    else{
        printf("moving UP [cost: %d]\n", moveCost);
        robotPos.y--; //move towards the request direction (int this case UP)
        CheckTile(0); //Check the tile the bot has landed on
        xtimer_usleep(400000);
    }
    return true;
}

bool sDown(int arg)
{
    (void)arg;
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove || OutOfBounds(robotPos.y, 1))
    {
        return false;
    }
    CheckPower(moveCost); //Check the state of power
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove)
    {
        return false;
    }
    //Determines the move to see if it is ok to do given the locations
    //known for where comunicated mines are
    bool move = isUnsafe(robotPos.x, robotPos.y + 1);
    //Try navigate around the mine by first trying to set x to + 1, then
    //y to + 1 3 times and then x to - 1, if not possible to go x + 1
    //(When a wall is in the way), then try x - 1 (To go backwards)
    //then y + 1 3 times and x + 1 to get around
    if(move){
        puts("\nAttempting to make way around communicated mine\n");
        //First checks if it can get past by moving in y direction 3 times
        if((robotPos.y + 3) <= boundaryY){
            //Checks which way to go around the mine
            if((robotPos.x + 1) <= boundaryX){
                avoidRight(0);
                //Moves along y 3 times
                avoidDown(0);
                avoidDown(0);
                avoidDown(0);
                //Moves back to the positon after the mine
                avoidLeft(0);
	        }
            else if((robotPos.x - 1) >= 0){
                avoidLeft(0);
                //Moves along y 3 times
                avoidDown(0);
                avoidDown(0);
                avoidDown(0);
                //Moves back to the positon after the mine
                avoidRight(0);
		    }
	    }
        //Otherwise attempt to only move 2, check if there is room
        // to move aroundwithout hiting a wall
        else if((robotPos.x + 1) <= boundaryX){
            avoidRight(0);
            //Moves along y 2 times
            avoidDown(0);
            avoidDown(0);
	    }
        //Otherwise is on the final row and there is no reason to move past
        //Send back 0 to let know that the job is finished
	}
    else{
        printf("moving DOWN [cost: %d]\n", moveCost);
        robotPos.y++;
        CheckTile(0);
        xtimer_usleep(400000);
	}
  return true;



}

bool sLeft(int arg)
{
    (void)arg;
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove || OutOfBounds(robotPos.x, 2))
    {
        return false;
    }
    CheckPower(moveCost); //Check the state of power
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove)
    {
        return false;
    }
    //Determines the move to see if it is ok to do given the locations
    //known for where comunicated mines are
    bool move = isUnsafe(robotPos.x - 1, robotPos.y);
    if(move){
      puts("\nAttempting to make way around communicated mine\n");
        //First checks if it can get past by moving in x direction 3 times
        if((robotPos.x - 3) >= boundaryX){
            //Checks which way to go around the mine
            if((robotPos.y + 1) <= boundaryX){
                avoidDown(0);
                //Moves along x 3 times
                avoidLeft(0);
                avoidLeft(0);
                avoidLeft(0);
                //Moves back to the positon after the mine
                avoidUp(0);
	        }
            else if((robotPos.y - 1) >= 0){
                avoidUp(0);
                //Moves along x 3 times
                avoidLeft(0);
                avoidLeft(0);
                avoidLeft(0);
                //Moves back to the positon after the mine
                avoidDown(0);
		    }
	    }
        //Otherwise attempt to only move 2, check if there is room
        // to move aroundwithout hiting a wall
        else if((robotPos.x + 1) <= boundaryX){
            avoidDown(0);
            //Moves along x 2 times
            avoidLeft(0);
            avoidLeft(0);
	    }
        //Otherwise is on the final row and there is no reason to move past
        //Send back 0 to let know that the job is finished
	}
    else{
        printf("moving LEFT [cost: %d]\n", moveCost);
        robotPos.x--;
        CheckTile(0);
        xtimer_usleep(400000);
    }
    return true;
}

bool sRight(int arg)
{
    (void)arg;
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove || OutOfBounds(robotPos.x, 3))
    {
        return false;
    }
    CheckPower(moveCost); //Check the state of power
    //Check if the bot is able to move at all (or move in this direction)
    if (!isFunctional || !canMove)
    {
        return false;
    }
    //Determines the move to see if it is ok to do given the locations
    //known for where comunicated mines are
    bool move = isUnsafe(robotPos.x + 1, robotPos.y);
    if(move){
      puts("\nAttempting to make way around communicated mine\n");
        //First checks if it can get past by moving in x direction 3 times
        if((robotPos.x + 3) <= 3){
            //Checks which way to go around the mine
            if((robotPos.y + 1) <= boundaryX){
                avoidDown(0);
                //Moves along x 3 times
                avoidRight(0);
                avoidRight(0);
                avoidRight(0);
                //Moves back to the positon after the mine
                avoidUp(0);
	        }
            else if((robotPos.y - 1) >= 0){
                avoidUp(0);
                //Moves along x 3 times
                avoidRight(0);
                avoidRight(0);
                avoidRight(0);
                //Moves back to the positon after the mine
                avoidDown(0);
		    }
	    }
        //Otherwise attempt to only move 2, check if there is room
        // to move aroundwithout hiting a wall
        else if((robotPos.x + 1) <= boundaryX){
            avoidDown(0);
            //Moves along x 2 times
            avoidRight(0);
            avoidRight(0);
	    }
        //Otherwise is on the final row and there is no reason to move past
        //Send back 0 to let know that the job is finished
	}
    else{
        printf("moving RIGHT [cost: %d]\n", moveCost);
        robotPos.x++;
        CheckTile(0);
        xtimer_usleep(400000);
    }
    return true;
}
//..

//Creates Status Message for either sending/displaying
//This Status outlines (id, current energy, current x, current y)
char* getSta(int argc)
{
    (void)argc;
    //printf("getting STATUS\n");
    char* str;
    str = malloc(sizeof(char) * 128);
    sprintf(str, "STATUS robot %d ENERGY %d at (%d, %d), survivors: ", id, energy, robotPos.x, robotPos.y);
    //puts(str);
    return str;
}

//A methoid for saving the location for a mine
void plotMine(int posX, int posY){
    //Adds to the array at the newest positon
    alertedMinesListX[alertedMinesListIndex] = posX;
    alertedMinesListY[alertedMinesListIndex] = posY;
    printf("\nSaving Bomb Location X = %d, Y = %d\n", alertedMinesListX[alertedMinesListIndex], alertedMinesListY[alertedMinesListIndex]);
    alertedMinesListIndex++;
}

//UNDER HERE IS WHERE WE WANT TO DEFINE OUR ROUTINES:
//These are standard C methods (find out how to define them)
//that can be called wherever in the code

//A methoid which takes in the starting position and ending Position
//If a value greater than -1 for the X and Y are passed in for the
//resumeX and resumeY, then make way to that location first
void performJob(int startingX, int startingY, int endX, int endY, int resumeX, int resumeY, int remainT) {
    printf("Remaining = %d", remainT);
    //Finds the x and y cordinates that the robot is currently at
    int x_location = robotPos.x;
    int y_location = robotPos.y;
    //Stores the begining location for the robot to naviagte to
    int beginX = 0;
    int beginY = 0;
    //Checks if the value of resumeX and resumeY are not negitive
    //to know if the job being perfomed is one being resumed
    if ((resumeX != -1) && (resumeY != -1)){
        printf("Resuming From Location X = %d, Y = %d\n", x_location, y_location);
        //sets beginX and beginY to be the resumeX/Y
        beginX = resumeX;
        beginY = resumeY;
	}
    else{
        printf("Starting From Location X = %d, Y = %d\n", x_location, y_location);
        //sets beginX and beginY to be the startingX/Y
        beginX = startingX;
        beginY = startingY;
	}
    //Goes through a sequence of trying to naviagte to the begining location

    //Decides if it either needs to subtract from the cordinates to get
    //to the passed in startingX / startingY cordinates
    if (x_location < beginX) {
        //Goes through a loop until it gets a location
        for (int i = x_location; i < beginX; i++) {
            //Navigates right to reach the starting x
            if(robotPos.x != beginX){
                printf("Going Right X = %d, Y = %d\n", robotPos.x, robotPos.y);
                sRight(0);
			}
        }
    }
    else if (beginX < x_location) {
        //Goes through a loop until it gets a location
        for (int i = beginX; i < x_location; i++) {
            if(robotPos.x != beginX){
                printf("Going Left X = %d, Y = %d\n", robotPos.x, robotPos.y);
                //Navigates left to reach the starting x
                sLeft(0);
			}
        }
    }
    //Otherwise the location the robot is currently at is the same as
    //the passed in starting x postion


    //Decides if it either needs to subtract from the cordinates to get
    //to the passed in startingX / startingY cordinates
    if (y_location < beginY) {
        //Goes through a loop until it gets a location
        for (int i = y_location; i < beginY; i++) {
            if(robotPos.y != beginY){
                printf("Going Down X = %d, Y = %d\n", robotPos.x, robotPos.y);
                //Navigates down to reach the starting y
                sDown(0);
            }
        }
    }
    else if (beginY < y_location) {
        //Goes through a loop until it gets a location
        for (int i = beginY; i < y_location; i++) {
            if(robotPos.y != beginY){
                printf("Going Up X = %d, Y = %d\n", robotPos.x, robotPos.y);
                //Navigates up to reach the starting y
                sUp(0);
            }
        }
    }
    //Otherwise the location the robot is currently at is the same as
    //the passed in starting y postion

    //Calculates how many tiles the robot needs to go across and
    //down by
    int across = endX - robotPos.x;
    int down = endY - robotPos.y;
    //Assigns the values for going through the loop
    int a = across;
    int d = down;
    //Calculates the number of remaining tiles which need to
    //
    int remaining = remainT;
    //printf("Remaining = %d\n", remaining);
    sleep(1);
    //Goes through a loop of doing the area assigned utill the number
    //of tiles remaining has gotten to 0
    //while (remaining > 0 && energy > 100) {
    while (energy > 100) {
        //Checks if the cordinate is equivelent to the
        //ending location
        if((robotPos.x == endX)&&(robotPos.y == endY)&&(remaining <= 0)){
            //Checks first if the remaining tiles is
            //close to what is expected
            //Send the job finsihed flag
            strcpy(bufMessage, "e");
            sendMessage(0);
		}
        //As a backup check if the number remaing is less than 0
        //In the case that the end point is avoided constantly
        //due to it being a bomb
        else if(remaining <= 0){
                //Send the job finsihed flag
                strcpy(bufMessage, "e");
                sendMessage(0);
                break;
		}
        //Checks if the robot needs to go backwards or forwards
        //to move across the area needed
        if (a > 0) {
            //Goes through the each of the rows across until it decrements
            //to 0
            while (a != 0 && energy > 100) {
                //Goes to the right across the map
                sRight(0);
                a--;
                //Subtracts one from the number of squares remaing
                remaining = remaining - 1;
                //printf("Remaining = %d\n", remaining);
            }
        }
        //
        else if (a == 0) {
            //Goes through the each of the rows across until it increments
            //back to the start
            while (a != across && energy > 100) {
                //Goes to the left across the map
                sLeft(0);
                a++;
                //Subtracts one from the number of squares remaing
                remaining = remaining - 1;
                //printf("Remaining = %d\n", remaining);
            }
        }
        //If there are still tiles which need to be navigated down
        //go down once at a time
        if (d > 0) {
            sDown(0);
            d--;
            //Subtracts one from the number of squares remaing
            remaining = remaining - 1;
            //printf("Remaining = %d\n", remaining);
        }
    }
    //Once finished, change the index for alertedMinesListIndex
    //to allow for the new mines to be recieved when a job is given
    alertedMinesListIndex = 0;
    //'e' is the end flag sent to the controller that the
    //robot has finished with the command
    strcpy(bufMessage, "e");
    sendMessage(0);
}

//This is a DEBUG method to test the bounds automatically
//This will be removed in the final build
void testBounds(int argc)
{
    puts("RUNNING DEBUG BOUNDS TEST");
    (void)argc;
    bool canMoveForward = true;
    while (canMoveForward == true)
    {
        canMoveForward = sRight(0);
    }
    xtimer_usleep(400000);
    strcpy(bufMessage, getSta(0));
    sendMessage(0);
    canMoveForward = true;
    while (canMoveForward == true)
    {
        canMoveForward = sDown(0);
    }
    xtimer_usleep(400000);
    strcpy(bufMessage, getSta(0));
    sendMessage(0);
}
//DEBUG: Testing mine behaviour
void testMines(int argc)
{
    puts("RUNNING DEBUG MINES TEST");
    (void)argc;
    sRight(0);
    bool canMoveForward = true;
    while (canMoveForward == true)
    {
        canMoveForward = sDown(0);
    }
}
//DEBUG: Testing survivor behaviour
void testSurvivor(int argc)
{
    puts("RUNNING DEBUG SURVIVOR TEST");
    (void)argc;
    //Survivor 1
    sRight(0);
    sRight(0);
    for (int i = 0; i < 11; i++)
    {
        sDown(0);
    }
    //Survivor 2
    for (int i = 0; i < 18; i++)
    {
        sRight(0);
    }
    //Survivor 3
    bool canMoveForward = true;
    while (canMoveForward == true)
    {
        canMoveForward = sUp(0);
    }
}
//DEBUG: Testing power behaviour
void testPower(int argc)
{
    puts("RUNNING DEBUG POWER TEST");
    (void)argc;
    while (canMove)
    {
        if (sRight(0))
        {

        }
        else
        {
            sLeft(0);
        }
    }
    while (isFunctional)
    {
        xtimer_usleep(400000);
        strcpy(bufMessage, "PP");
        sendMessage(0);
    }
}
//..

//This method sends the global array [bufMessage]
//the usage of this method is as so:
//strcpy(bufMessage, <Message you want to display>);
//sendMessage(0);
//..
void sendMessage(int argc)
{
    (void)argc;
    cost = strlen((char*)bufMessage);
    CheckPower(cost);
    if (!isFunctional)
    {
        memset(bufMessage, 0, sizeof(bufMessage));
    }
    else
    {
        printf("\nsending [%s] back to controller [cost: %d]\n", (char*)bufMessage, cost);
        if (sock_udp_send(&sock, bufMessage, strlen((char*)bufMessage) + 1, &remote) < 0)
        {
            puts("\nError sending reply to client");
        }
        memset(bufMessage, 0, sizeof(bufMessage));
    }
}
//..



//This is resposbible for reading the commands given by the controller
//This will determine what to run in the robot program
void readCommand(char* argv)
{
    printf("Running %s", argv);
    //Define a Key here
    //Keys are used as the 'commands' entered by the user
    //at the controller end
    char* command = argv;
    char up[] = "up";
    char down[] = "down";
    char left[] = "left";
    char right[] = "right";
    char sta[] = "s";
    char job[] = "j";
    char bomb[] = "b";
    char resumeJob[] = "r";

    //DEBUG: VALUES
    char bound[] = "runBound";
    char power[] = "runPower";
    char mines[] = "runMines";
    char survivors[] = "runRescue";

    printf("%s\n", command);

    char strCheck[128];
    strcpy(strCheck, argv);
    char* token = strtok(strCheck, ",");

    int strCount = 0;
    char* jobArg[24];
    //Stores the argument for the for a mine alert
    char* mineArg[2];
    //Reads the ID of the message
    char* messageID[2];

    //Reads the id from the mesaage
    messageID[0] = token;
    token = strtok(NULL, ",");
    //Checks if the value of the message ID
    //is the same as the robot ID
    if(atoi(messageID[0]) == ROBOT_ID){
        //Otherwise the message needs to be split
        //as it is a message that needs parmeters to
        //sent
        if (strcmp(token, job) == 0)
        {
            while (token != NULL)
            {
                jobArg[strCount - 1] = token;
                strCount++;
                token = strtok(NULL, ",");
            }
            performJob(atoi(jobArg[0]), atoi(jobArg[1]), atoi(jobArg[2]), atoi(jobArg[3]), -1, -1, atoi(jobArg[4]));
        }
        else if (strcmp(token, resumeJob) == 0)
        {
            while (token != NULL)
            {
                jobArg[strCount - 1] = token;
                strCount++;
                token = strtok(NULL, ",");
            }
            performJob(atoi(jobArg[0]), atoi(jobArg[1]), atoi(jobArg[2]), atoi(jobArg[3]), atoi(jobArg[4]), atoi(jobArg[5]), atoi(jobArg[6]));
        }
        else if (strcmp(token, bomb) == 0){
            while (token != NULL)
            {
                mineArg[strCount - 1] = token;
                strCount++;
                token = strtok(NULL, ",");
            }
            //Runs the plot mine methoid to save the mine location to an array
            plotMine(atoi(mineArg[0]), atoi(mineArg[1]));
	    }
        //Test Values: 10 10 20 20
        //bunch of GROSS if statments to figure out what was told to
        //run. (BETTER IMPLEMENTATION IS WELCOME)
        else if (strcmp(command, up) == 0)
        {
            sUp(0);
        }
        else if (strcmp(command, down) == 0)
        {
            sDown(0);
        }
        else if (strcmp(command, left) == 0)
        {
            sLeft(0);
        }
        else if (strcmp(command, right) == 0)
        {
            sRight(0);
        }
        else if (strcmp(command, sta) == 0)
        {
            strcpy(bufMessage, getSta(0));
            sendMessage(0);
        }

        //DEBUG: METHODS
        else if (strcmp(command, bound) == 0)
        {
            testBounds(0);
        }
        else if (strcmp(command, power) == 0)
        {
            testPower(0);
        }
        else if (strcmp(command, mines) == 0)
        {
            testMines(0);
        }
        else if (strcmp(command, survivors) == 0)
        {
            testSurvivor(0);
        }
	}
}





// Thie Following Methods are the command line methods that call all the
// fuction methods in a certain way. THIS IS WHERE WE PUT ROUTINES.
extern int testBoundariesR(int argc, char** argv)
{
    if (argc != 1)
    {
        printf("usage: %s\n", argv[0]);
        return 1;
    }
    testBounds(0);
    return 0;
}

extern int sUp_cmd(int argc, char** argv)
{
    if (argc != 1)
    {
        printf("usage: %s\n", argv[0]);
        return 1;
    }
    sUp(0);
    return 0;
}

extern int sDown_cmd(int argc, char** argv)
{
    if (argc != 1)
    {
        printf("usage: %s\n", argv[0]);
        return 1;
    }
    sDown(0);
    return 0;
}

extern int sLeft_cmd(int argc, char** argv)
{
    if (argc != 1)
    {
        printf("usage: %s\n", argv[0]);
        return 1;
    }
    sLeft(0);
    return 0;
}

extern int sRight_cmd(int argc, char** argv)
{
    if (argc != 1)
    {
        printf("usage: %s\n", argv[0]);
        return 1;
    }
    sRight(0);
    return 0;
}

extern int getSta_cmd(int argc, char** argv)
{
    if (argc != 1)
    {
        printf("usage: %s\n", argv[0]);
        return 1;
    }
    getSta(0);
    return 0;
}
//..



//This thread is responsible for constantly listening for commands from the controller
//It usually just opens read commands but it is responsible for opening and closing
//connections from the controller
void* controller_listner(void* args)
{
    (void)args;
    // prepare a local address
    sock_udp_ep_t local = SOCK_IPV6_EP_ANY;
    local.port = ROBOT_PORT + ROBOT_ID;

    // create a UDP sock bound to that local address
    if (sock_udp_create(&sock, &local, NULL, 0) < 0)
    {
        puts("Error creating UDP sock");
        return NULL;
    }
    printf("\nserver waiting on port %d\n", local.port);

    ssize_t res;
    while (1)
    {
        //polling reading the socket for a packet
        res = sock_udp_recv(&sock, buf, sizeof(buf), 3 * US_PER_SEC, &remote);
        if (res >= 0)
        {
            //Convert IVP6 to readable
            char ipv6_addr_str[IPV6_ADDR_MAX_STR_LEN];
            if (ipv6_addr_to_str(ipv6_addr_str, (ipv6_addr_t*)&remote.addr.ipv6, IPV6_ADDR_MAX_STR_LEN) == NULL)
            {
                strcpy(ipv6_addr_str, "???");
            }
            //Displays in console what was recieved and applys the energy cost
            cost = strlen((char*)buf);
            CheckPower(cost);
            if (!isFunctional)
            {
                puts("No longer functional\n");
                break;
            }
            buf[res] = 0; // ensure null-terminated string
            printf("\nRecieved from (%s, %d): %s (cost: %d)", ipv6_addr_str, remote.port, (char*)buf, cost);
            //'e' is the start flag sent to the controller that the
            //robot is actively doing the command
            strcpy(bufMessage, "i");
            sendMessage(0);
            if (!isFunctional)
            {
                break;
            }
            //This is decides what needs to be done
            readCommand((char*)buf);
        }
        else {
            puts("no Comand");
		}
    }
    return NULL;
}



//This commands[] array is for storing the cmd comands that can
//be called from the robot's console
static const shell_command_t commands[] =
{
    {"sUp", "move UP by 1 tile", sUp_cmd},
    {"sDown", "move DOWN by 1 tile", sDown_cmd},
    {"sLeft", "move LEFT by 1 tile", sLeft_cmd},
    {"sRight", "move RIGHT by 1 tile", sRight_cmd},
    {"getSta", "show STATUS", getSta_cmd},
    {"TBRoutine", "DEBUG: testing BOUNDS routine", testBoundariesR},
    {NULL, NULL, NULL}
};

//Main Program that runs on start up and sets all the needed
//Paramters/Parses the need values from the MakeFile/Sets up Bot
int main(void)
{
    // print network addresses. this is useful to confirm the right addresses are being used
    puts("Configured network interfaces:");
    _gnrc_netif_config(0, NULL);

    //Set RobotPosition to 0,0
    robotPos.x = 0;
    robotPos.y = 0;

    //Get mine locations from the MakeFile for the CheckTile Method
    char str[128];
    strcpy(str, MINES_LIST);
    char* token = strtok(str, ",");
    int count = 0;
    //Stores the mine locations in a position struct array
    while (token != NULL)
    {
        struct position Npos;
        Npos.x = atoi(token);
        token = strtok(NULL, ",");
        Npos.y = atoi(token);
        mineList[count] = Npos;
        count++;
        mineCount++;
        token = strtok(NULL, ",");
    }
    //DEBUG: LIST ALL THE GIVEN MINES ON THE MAP
    for (int i = 0; i < mineCount; i++)
    {
        struct position curBomb = mineList[i];
        printf("Bomb at Positon: (%d, %d) \n", curBomb.x, curBomb.y);
    }

    //Get survivor locations from the MakeFile for the CheckTile Method
    char str2[NUM_COLUMNS * 3];
    strcpy(str2, SURVIVOR_LIST);
    token = strtok(str2, ",");
    count = 0;
    //Parses and Stores all the suriviors in a position struct array
    while (token != NULL)
    {
        struct position Npos;
        Npos.x = atoi(token);
        token = strtok(NULL, ",");
        Npos.y = atoi(token);
        survivorList[count] = Npos;
        count++;
        survivorCount++;
        token = strtok(NULL, ",");
    }
    //DEBUG: LIST ALL THE GIVEN SURVIVORS ON THE MAP
    for (int i = 0; i < survivorCount; i++)
    {
        struct position curSur = survivorList[i];
        printf("Survivor at Positon: (%d, %d) \n", curSur.x, curSur.y);
    }

    //Create thread listener
    thread_create(stack,
        sizeof(stack),
        THREAD_PRIORITY_MAIN - 1,
        THREAD_CREATE_STACKTEST,
        controller_listner,
        (void*)&id,
        "remote_listener");

    // configure the underlying network such that all packets transmitted will reach a server
    //ipv6_addr_set_all_nodes_multicast((ipv6_addr_t*)&remote.addr.ipv6, IPV6_ADDR_MCAST_SCP_LINK_LOCAL);
    printf("Bot %d Ready...\n", id);



    //Initialize CMD command shell
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(commands, line_buf, SHELL_DEFAULT_BUFSIZE);
}
