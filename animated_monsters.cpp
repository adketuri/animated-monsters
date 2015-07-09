/*
	animated_monsters.cpp
	
	A DynRPG plugin to animate your monsters with the DBS!
	
	--- 1. Poses ---
	11 poses per monster are required, named 1.png, 2.png, and so on.
	Dump your assets in \Monster\<Name>\1.png, where <Name> is the name of the monster in your database.
	1-3.png = idle, 4.png = dead, 5-6.png hurt, 7-8.png approach/return, 9-11.png attack
	
	--- 2. Movement ---
	By default, all attacks and skills will approach the target. If you'd like to have a character not 
	approach (eg, for a ranged spell), you will need to modify the onStartup callback and *recompile*. 
	This was a deliberate decision on my part three years ago to obfuscate some of the implementation 
	details and prevent others from tinkering with ini files. If implemented today I'd likely revisit 
	this decision.
	
	--- 3. Casting ---
	If any attacks are set to not approach, a cast animation is used. These follow similar naming conventions
	as the poses. Name the individual frames 1.png, 2.png, etc., and dump them in Picture\cast\
	CAST_SPRITES defines how many frames you will have. Alpha is automatically added.
	Battle Animations should accommodate for casting with 8-10 blank frames at the start and a sound effect.
	
	--- 4. Known issues ---
	* Pincer/back attacks display the old monsters or crash the game. 
	* The animation logic assumes 60fps. Things get weird otherwise.
	* Sometimes players can attack a monster mid-attack. They will run to to a point on the monster's path.
	
	Feel free to use or modify this as you see fit. Redistribution with source is appreciated.

	Author: Andrew Keturi (http://andrewis.cool)
	Last modified: 6 JUN 2012 
	Source re-release: 9 JUL 2015 
*/

#define AUTO_DLLMAIN
#define NUM_POSES 12
#define MAX_MONSTERS 8
#define CAST_SPRITES 13

#include <DynRPG/DynRPG.h>
#include <sstream>

RPG::Image* castAnimation[CAST_SPRITES];
RPG::Image* monsterPoses[MAX_MONSTERS][NUM_POSES];						/* table containing all images created */
RPG::Image* oldPose;													/* rm2k3's old monster pose, saved off */
boolean resetTable = false;
int max = 0;															/* reset table flag */
int frameTimer = 0;														/* global frame updater */
int hurtTimer[MAX_MONSTERS];											/* a timer for how many frames we ought to show the hurt pose */
int oldHP[MAX_MONSTERS];												/* old hp vals to check for damaged enemy */
int poseIndex[MAX_MONSTERS];											/* which monster pose do we show? */
int animationCycle[NUM_POSES] = {1, 2, 3, 2, 4, 5, 6, 7, 8, 9, 10, 11};	/* index for which animation file to display */
int animationType; 														/* which animation type? 0=idle, 1=hurt, 2=dead */
/* 1-3 idle, 4 dead, 5-6 hurt, 7-8 approach/return, 9-11 attack */

int castTimer = 0;														/* timer for the cast animation */
int castX, castY;
bool doCastAnimation = false;
int monsterSrcX[MAX_MONSTERS];
int monsterSrcY[MAX_MONSTERS];
int monsterDestX[MAX_MONSTERS];
int monsterDestY[MAX_MONSTERS];
int monsterX[MAX_MONSTERS];
int monsterY[MAX_MONSTERS];
int monsterMoveFrame[MAX_MONSTERS];
bool monsterApproach[MAX_MONSTERS];
bool monsterCast[MAX_MONSTERS];
bool monsterSrcSaved[MAX_MONSTERS];
bool setInitialRandAtb[MAX_MONSTERS];
int dist;
bool castingTable[1000];
short jumpOffset[] = {0, -3, -5, -7, -9, -11, -12, -13, -13, -14, -14, -13, -13, -12, -11, -9, -7, -5, -3, -0};
short jumpTick = 0;

// Initialize pose types on-startup
bool onStartup (char *pluginName)
{
    for (int i = 0; i < 1000; i++)
    {
        castingTable[i] = false;
    }
	// IMPORTANT! flag skill id's (from 2k3's db) here to show casting animations.
    //castingTable[98] = true; 		// dark strike
	//castingTable[96] = true; 		// earth spike
	//castingTable[97] = true;		// poison
    return true;
}

// destroy ALL the images!!
void initializeBattle()
{

    // update our initial pose indexes (0-3)
    for (int i=0; i<MAX_MONSTERS; i++)
    {
        poseIndex[i]=i%4;
        hurtTimer[i]=0;
        oldHP[i]=0;
        monsterX[i] = 0;
        monsterY[i] = 0;
        monsterSrcX[i] = 0;
        monsterSrcY[i] = 0;
        monsterDestX[i] = 0;
        monsterDestY[i] = 0;
        monsterMoveFrame[i] = 0;
        monsterApproach[i] = false;
        monsterCast[i] = false;
        monsterSrcSaved[i] = false;
		setInitialRandAtb[i] = false;

        // destroy any remaining images -- no leaks pls
        for (int j=0; j<NUM_POSES; j++)
        {
            if(monsterPoses[i][j])
            {
                RPG::Image::destroy(monsterPoses[i][j]);
            }
        }
    }
}

// called every time a batter is drawn
bool onDrawBattler(RPG::Battler *battler, bool isMonster, int id)
{

    // we will be updating the battler dynamically iff the battler is of type RPG::Monster
    if(isMonster)
    {

        // save off the source and destination x/y coords
        if (!monsterSrcSaved[id])
        {
            monsterSrcX[id] = battler->x;
            monsterSrcY[id] = battler->y;
            monsterX[id] = battler->x;
            monsterY[id] = battler->y;
            monsterSrcSaved[id] = true;
        }

        // if the current HP is less than old HP, we need to set the appropriate hurt timer
        // else: reset oldhp in the case where the enemy is healed
        if(battler->hp < oldHP[id])
        {
            hurtTimer[id] = 30;
            oldHP[id] = battler->hp;
        }
        else if(battler->hp > oldHP[id])
        {
            oldHP[id] = battler->hp;
        }

        // if the monster has hurt poses to display, we need to hard set the frame index to 5 (HURT)
        if(hurtTimer[id] > 0)
        {
            poseIndex[id] = 5 + (frameTimer-1)/5;
            hurtTimer[id]--;
        }

        // if the monster dies, we want to hard set the frame index to 4 (DEAD) and stop it if it's moving
        if(battler->hp <= 0)
        {
            poseIndex[id]=4;
            monsterApproach[id] = false;
        }

        // if the monster is attacking, let's update the X/Y coords
        if (monsterSrcSaved[id])
        {
            battler->x = monsterX[id];
            battler->y = monsterY[id];
        }

        // check if we need to load the monster pic
        if(!monsterPoses[id][animationCycle[poseIndex[id]]])
        {
            // create the new blank image
            monsterPoses[id][animationCycle[poseIndex[id]]] = RPG::Image::create();
            monsterPoses[id][animationCycle[poseIndex[id]]]->useMaskColor = true;

            // load the image file into our image we created
            std::stringstream fileName;
            fileName << "Monster\\" << battler->getName() << "\\" << animationCycle[poseIndex[id]] << ".png";
            monsterPoses[id][animationCycle[poseIndex[id]]]->loadFromFile(fileName.str(), true);
        }

        // we do this (UGLY) cast to update the monster's image to the image in the loader table
        oldPose = static_cast<RPG::Monster*>(battler)->image;
        static_cast<RPG::Monster*>(battler)->image = monsterPoses[id][animationCycle[poseIndex[id]]];

    }
    return true;
}

// after the battler is drawn, we need to set the monster's default image to point back to the saved off image
bool onBattlerDrawn(RPG::Battler *battler, bool isMonster, int id)
{
    if (isMonster)
    {
        // todo SET old origin of monster for player attacking
        if (monsterSrcSaved[id])
        {
            // TODO enable this
            //battler->x = monsterSrcX[id];
            //battler->y = monsterSrcY[id];
        }
        // set to old pose
        static_cast<RPG::Monster*>(battler)->image = oldPose;
    }

    // update cast animation, if present
    if (doCastAnimation)
    {
        if (castTimer < CAST_SPRITES*7)
        {
            if (castAnimation[castTimer/7])
            {
                RPG::screen->canvas->draw(castX-48, castY-48-20, castAnimation[castTimer/7]);
            }
            else
            {
                // create pics
                for (int i = 0; i < CAST_SPRITES; i++)
                {
                    if(!castAnimation[i])
                    {
                        castAnimation[i] = RPG::Image::create();
                        castAnimation[i]->useMaskColor = true;
                        std::stringstream fileName;
                        fileName << "Picture\\cast\\" << (i + 1) << ".png";
                        castAnimation[i]->loadFromFile(fileName.str(), true);
                        castAnimation[i]->alpha = 120;
                    }
                }
            }
            castTimer++;

        }
        else
        {
            castTimer = 0;
            doCastAnimation = false;
        }
    }

    return true;
}

// called when a battler performs an action. we're only interested in monsters.
bool onDoBattlerAction (RPG::Battler *battler)
{
    if (battler->isMonster())
    {
		// prepare for movement
        monsterCast[battler->id-1] = true;
        monsterDestX[battler->id-1] = battler->x + 10;				// default case, just move forward 10px
        monsterDestY[battler->id-1] = battler->y;
        if (battler->action->target == RPG::TARGET_ACTOR && !castingTable[battler->action->skillId])
        {
            monsterDestX[battler->id-1] = RPG::Actor::partyMember(battler->action->targetId)->x - 20;
            monsterDestY[battler->id-1] = RPG::Actor::partyMember(battler->action->targetId)->y;
            monsterCast[battler->id-1] = false;
        }
        monsterMoveFrame[battler->id-1] = 0;

        // flag monster to approach ally
        monsterApproach[battler->id-1] = true;

    }
    return true;
}

// called every frame refresh
void onFrame(RPG::Scene scene)
{

    // for battle scenes only, we update a frame timer
    if(RPG::system->scene == RPG::SCENE_BATTLE)
    {
        frameTimer++;

        // every ten frames, update the index to be displayed in our monster's image
        if(frameTimer > 10)
        {
            frameTimer=0;
            for(int i=0; i<MAX_MONSTERS; i++)
            {
                if(hurtTimer[i]==0 && !monsterApproach[i])
                {
                    poseIndex[i] = (poseIndex[i]+1)%4;
                }
            }
        }

        // update the monsters that are moving
        for(int i=0; i<MAX_MONSTERS; i++)
        {
            // update for approaches
            if (monsterApproach[i])
            {

                if (!monsterCast[i])
                {

                    // APPROACH THE PLAYER UPDATES
                    if (monsterMoveFrame[i] < 20)
                    {
                        poseIndex[i] = 7;
                        monsterX[i] = monsterSrcX[i] + ((monsterDestX[i] - monsterSrcX[i]) * monsterMoveFrame[i] / 20);
                        monsterY[i] = monsterSrcY[i] + ((monsterDestY[i] - monsterSrcY[i]) * monsterMoveFrame[i] / 20);

                    }

                    if (monsterMoveFrame[i] == 25)
                    {
                        poseIndex[i] = 9;
                    }
                    else if (monsterMoveFrame[i] == 30)
                    {
                        poseIndex[i] = 10;
                    }
                    else if (monsterMoveFrame[i] == 35)
                    {
                        poseIndex[i] = 11;
                        jumpTick = 0;
                    }

                    // get monsters current distance from it's starting position
                    dist = abs(monsterSrcX[i] - monsterX[i]) + abs(monsterSrcY[i] - monsterY[i]);
                    if (monsterMoveFrame[i] > 50 && dist > 3)
                    {

                        poseIndex[i] = 8;
                        monsterX[i] = monsterDestX[i] + ((monsterSrcX[i] - monsterDestX[i]) * (monsterMoveFrame[i]-50) / 20);
                        monsterY[i] = monsterDestY[i] + ((monsterSrcY[i] - monsterDestY[i]) * (monsterMoveFrame[i]-50) / 20);
						if (jumpTick < 20)
                    	{
                    		monsterY[i] += jumpOffset[jumpTick];
                    		jumpTick++;
                    	}
                    }

                    if (monsterMoveFrame[i] >= 71 && dist < 3)
                    {
                        poseIndex[i] = 1;
                        monsterX[i] = monsterSrcX[i];
                        monsterY[i] = monsterSrcY[i];
                        monsterApproach[i] = false;
                        monsterMoveFrame[i] = 0;
                    }

                }
                else
                {
                    // CASTING UPDATES

                    // jump forward a few
                    if (monsterMoveFrame[i] < 5)
                    {
                        poseIndex[i] = 7;
                        monsterX[i] = monsterSrcX[i] + ((monsterDestX[i] - monsterSrcX[i]) * monsterMoveFrame[i] / 5);
                        monsterY[i] = monsterSrcY[i] + ((monsterDestY[i] - monsterSrcY[i]) * monsterMoveFrame[i] / 5);

                    }

                    // (activate cast)
                    if (monsterMoveFrame[i] == 10)
                    {
                        castX = monsterX[i];
                        castY = monsterY[i];
                        doCastAnimation = true;
                    }

                    if (monsterMoveFrame[i] == 15)
                    {
                        poseIndex[i] = 9;
                    }
                    else if (monsterMoveFrame[i] == 20)
                    {
                        poseIndex[i] = 10;
                    }
                    else if (monsterMoveFrame[i] == 25)
                    {
                        poseIndex[i] = 11;
                    }

                    // get monsters current distance from it's starting position
                    dist = abs(monsterSrcX[i] - monsterX[i]) + abs(monsterSrcY[i] - monsterY[i]);
                    if (monsterMoveFrame[i] > 70 && dist > 3)
                    {
                        poseIndex[i] = 8;
                        monsterX[i] = monsterDestX[i] + ((monsterSrcX[i] - monsterDestX[i]) * (monsterMoveFrame[i]-70) / 5);
                        monsterY[i] = monsterDestY[i] + ((monsterSrcY[i] - monsterDestY[i]) * (monsterMoveFrame[i]-70) / 5);
                    }

					// reset movement
                    if (monsterMoveFrame[i] > 75 && dist < 3)
                    {
                        poseIndex[i] = 1;
                        monsterX[i] = monsterSrcX[i];
                        monsterY[i] = monsterSrcY[i];
                        monsterApproach[i] = false;
                        monsterCast[i] = false;
                        monsterMoveFrame[i] = 0;
                    }
                }
                monsterMoveFrame[i]++;
            }
        }
        // set a flag notifying that we need to reset the loader table after combat
        resetTable = true;
    }
    else if (resetTable)
    {
        // reset the loader table post-battle
        initializeBattle();
        resetTable = false;
    }
}
