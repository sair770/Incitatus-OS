/**
| Copyright(C) 2012 Ali Ersenal
| License: WTFPL v2
| URL: http://sam.zoy.org/wtfpl/COPYING
|
|--------------------------------------------------------------------------
| Module.c
|--------------------------------------------------------------------------
|
| DESCRIPTION:  Manages OS modules(components).
|
| AUTHOR:       Ali Ersenal, aliersenal@gmail.com
\------------------------------------------------------------------------*/


#include <Module.h>
#include <Debug.h>

/*=======================================================
    DEFINE
=========================================================*/
#define MAX_LOADED_MODULES    32

/*=======================================================
    PRIVATE DATA
=========================================================*/
PRIVATE Module* loadedModules[MAX_LOADED_MODULES];
PRIVATE u32int  numberOfLoadedModules = 0;

/*=======================================================
    FUNCTION
=========================================================*/
PUBLIC void Module_load(Module* module) {

    /* TODO: support for var-args in init function */

    /* Check if there is any place in loadedModules */
    Debug_assert(numberOfLoadedModules < MAX_LOADED_MODULES);

    /* Module should not be loaded already */
    Debug_assert(module->isLoaded == 0);

    /* Module should not share the same ID with another loaded module */
    for(u32int i = 0; i < numberOfLoadedModules; i++)
        Debug_assert(loadedModules[i]->moduleID != module->moduleID);

    /* Check dependencies, every dependency needs to be in loadedModules */
    for(u32int i = 0; i < module->numberOfDependencies; i++) {

        bool found = FALSE;

        for(u32int y = 0; y < numberOfLoadedModules; i++) {

            if(module->dependencies[i] == loadedModules[y]->moduleID) {

                found = TRUE;
                break;
            }

        }

        Debug_assert(found == TRUE);
    }

    /* call initialisation function of module */
    module->init();

    /* insert in loadedModules */
    loadedModules[numberOfLoadedModules] = module;
    module->isLoaded = 1;
    numberOfLoadedModules++;

}

PUBLIC void Module_unload(Module* module) {

    /* TODO: Implement */
    UNUSED(module);

}