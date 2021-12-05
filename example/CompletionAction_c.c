// Copyright (c) 2020 Doug Binks
// 
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
// 
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgement in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#include "TaskScheduler_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enkiTaskScheduler*    pETS;

struct CompletionArgsA
{
    enkiTaskSet*          pTaskB;
    uint32_t              run;
};

struct CompletionArgsB
{
    enkiTaskSet*          pTaskA;
    enkiTaskSet*          pTaskB;
    enkiDependency*       pDependency;
    enkiCompletionAction* pCompletionActionA;
    enkiCompletionAction* pCompletionActionB;
    uint32_t              run;
};

// In this example all our TaskSet functions share the same args struct, but we could use different one
struct TaskSetArgs
{
    enkiTaskSet* pTask;
    const char*  name;
    uint32_t     run;
};

void CompletionFunctionPreComplete_ModifyDependentTask( void* pArgs_, uint32_t threadNum_ )
{
    struct CompletionArgsA* pCompletionArgs  = pArgs_;
    struct enkiParamsTaskSet paramsTaskNext = enkiGetParamsTaskSet( pCompletionArgs->pTaskB );

    printf("CompletionFunctionA Pre Complete for run %u running on thread %u\n",
            pCompletionArgs->run, threadNum_ );

    // in this function we can modify the parameters of any task which depends on this CompletionFunction
    // pre complete functions should not be used to delete the current CompletionAction, for that use PostComplete functions
    paramsTaskNext.setSize = 10; // modify the set size of the next task - for example this could be based on output from previous task
    enkiSetParamsTaskSet( pCompletionArgs->pTaskB, paramsTaskNext );

    // safe to free our own args in this example as no other function dereferences them
    free( pCompletionArgs );
}

void CompletionFunctionPostComplete_DeleteTasks( void* pArgs_, uint32_t threadNum_ )
{
    struct CompletionArgsB* pCompletionArgs = pArgs_;

    printf("CompletionFunctionB Post Complete for run %u running on thread %u\n",
           pCompletionArgs->run, threadNum_ );

    // free memory
    // note must delete a dependency before you delete the dependency task and the task to run on completion
    enkiDeleteDependency( pETS, pCompletionArgs->pDependency );

    free( enkiGetParamsTaskSet( pCompletionArgs->pTaskA ).pArgs );
    free( enkiGetParamsTaskSet( pCompletionArgs->pTaskB ).pArgs );
    enkiDeleteTaskSet( pETS, pCompletionArgs->pTaskA );
    enkiDeleteTaskSet( pETS, pCompletionArgs->pTaskB );

    enkiDeleteCompletionAction( pETS, pCompletionArgs->pCompletionActionA );
    enkiDeleteCompletionAction( pETS, pCompletionArgs->pCompletionActionB );

    // safe to free our own args in this example as no other function dereferences them
    free( pCompletionArgs );
}

void TaskSetFunc( uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ )
{
    (void)start_; (void)end_;
    struct TaskSetArgs* pTaskSetArgs        = pArgs_;
    struct enkiParamsTaskSet paramsTaskNext = enkiGetParamsTaskSet( pTaskSetArgs->pTask );
    if( 0 == start_ )
    {
         // for clarity in this example we only output one printf per taskset func called, but would normally loop from start_ to end_ doing work
        printf("Task %s for run %u running on thread %u has set size %u\n", pTaskSetArgs->name, pTaskSetArgs->run, threadnum_, paramsTaskNext.setSize);
    }

    // A TastSetFunction is not a safe place to free it's own pArgs_ as when the setSize > 1 there may be multiple
    // calls to this function with the same pArgs_
}


int main(int argc, const char * argv[])
{
    // This examples shows CompletionActions used to modify a following tasks parameters and free allocations
    // Task Graph for this example (with names shortened to fit on screen):
    // 
    // pTaskSetA
    //          ->pCompletionActionA-PreFunc-(no PostFunc)
    //                                      ->pTaskSetB
    //                                                ->pCompletionActionB-(no PreFunc)-PostFunc
    //
    // Note that pTaskSetB must depend on pCompletionActionA NOT pTaskSetA or it could run at the same time as pCompletionActionA
    // so cannot be modified.
    struct enkiTaskSet*               pTaskSetA;
    struct enkiCompletionAction*      pCompletionActionA;
    struct enkiTaskSet*               pTaskSetB;
    struct enkiCompletionAction*      pCompletionActionB;
    struct TaskSetArgs*               pTaskSetArgsA;
    struct CompletionArgsA*           pCompletionArgsA;
    struct enkiParamsCompletionAction paramsCompletionActionA;
    struct TaskSetArgs*               pTaskSetArgsB;
    struct enkiDependency*            pDependencyOfTaskSetBOnCompletionActionA;
    struct CompletionArgsB*           pCompletionArgsB;
    struct enkiParamsCompletionAction paramsCompletionActionB;
    int run;

    pETS = enkiNewTaskScheduler();
    enkiInitTaskScheduler( pETS );

    for( run=0; run<10; ++run )
    {
        // Create all this runs tasks and completion actions
        pTaskSetA          = enkiCreateTaskSet( pETS, TaskSetFunc );
        pCompletionActionA = enkiCreateCompletionAction( pETS,
                                                    CompletionFunctionPreComplete_ModifyDependentTask,
                                                    NULL );
        pTaskSetB          = enkiCreateTaskSet( pETS, TaskSetFunc );
        pCompletionActionB = enkiCreateCompletionAction( pETS,
                                                    NULL,
                                                    CompletionFunctionPostComplete_DeleteTasks );

        // Set args for TaskSetA
        pTaskSetArgsA    = malloc(sizeof(struct TaskSetArgs));
        pTaskSetArgsA->run   = run;
        pTaskSetArgsA->pTask = pTaskSetA;
        pTaskSetArgsA->name  = "A";
        enkiSetArgsTaskSet( pTaskSetA, pTaskSetArgsA );

        // Set args for CompletionActionA, and make dependent on TaskSetA through pDependency
        pCompletionArgsA = malloc(sizeof(struct CompletionArgsA));
        pCompletionArgsA->pTaskB = pTaskSetB;
        pCompletionArgsA->run    = run;
        paramsCompletionActionA = enkiGetParamsCompletionAction( pCompletionActionA );
        paramsCompletionActionA.pArgsPreComplete  = pCompletionArgsA;
        paramsCompletionActionA.pArgsPostComplete = NULL; // pCompletionActionB does not have a PostComplete function 
        paramsCompletionActionA.pDependency = enkiGetCompletableFromTaskSet( pTaskSetA );
        enkiSetParamsCompletionAction( pCompletionActionA, paramsCompletionActionA );


        // Set args for TaskSetB
        pTaskSetArgsB    = malloc(sizeof(struct TaskSetArgs));
        pTaskSetArgsB->run   = run;
        pTaskSetArgsB->pTask = pTaskSetB;
        pTaskSetArgsB->name  = "B";
        enkiSetArgsTaskSet( pTaskSetB, pTaskSetArgsB );

        // TaskSetB depends on pCompletionActionA
        pDependencyOfTaskSetBOnCompletionActionA = enkiCreateDependency( pETS );
        enkiSetDependency( pDependencyOfTaskSetBOnCompletionActionA,
                           enkiGetCompletableFromCompletionAction( pCompletionActionA ),
                           enkiGetCompletableFromTaskSet( pTaskSetB ) );

        // Set args for CompletionActionB, and make dependent on TaskSetB through pDependency
        pCompletionArgsB = malloc(sizeof(struct CompletionArgsB));
        pCompletionArgsB->pTaskA             = pTaskSetA;
        pCompletionArgsB->pTaskB             = pTaskSetB;
        pCompletionArgsB->pDependency        = pDependencyOfTaskSetBOnCompletionActionA;
        pCompletionArgsB->pCompletionActionA = pCompletionActionA;
        pCompletionArgsB->pCompletionActionB = pCompletionActionB;
        pCompletionArgsB->run                = run;

        paramsCompletionActionB = enkiGetParamsCompletionAction( pCompletionActionB );
        paramsCompletionActionB.pArgsPreComplete  = NULL; // pCompletionActionB does not have a PreComplete function
        paramsCompletionActionB.pArgsPostComplete = pCompletionArgsB;
        paramsCompletionActionB.pDependency = enkiGetCompletableFromTaskSet( pTaskSetB );
        enkiSetParamsCompletionAction( pCompletionActionB, paramsCompletionActionB );


        // To launch all, we only add the first TaskSet
        enkiAddTaskSet( pETS, pTaskSetA );
    }
    enkiWaitForAll( pETS );

    enkiDeleteTaskScheduler( pETS );
}
