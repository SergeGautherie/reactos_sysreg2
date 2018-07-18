/*
 * PROJECT:     ReactOS System Regression Testing Utility
 * LICENSE:     GNU GPLv2 or any later version as published by the Free Software Foundation
 * PURPOSE:     Main entry point and controlling the virtual machine
 * COPYRIGHT:   Copyright 2008-2009 Christoph von Wittich <christoph_vw@reactos.org>
 *              Copyright 2009 Colin Finck <colin@reactos.org>
 *              Copyright 2012-2015 Pierre Schweitzer <pierre@reactos.org>
 *              Copyright 2018 Serge Gautherie <reactos-git_serge_171003@gautherie.fr>
 */

#include "sysreg.h"
#include "machine.h"

const char DefaultOutputPath[] = "output-i386";
const char* OutputPath;
Settings AppSettings;
ModuleListEntry* ModuleList;

int main(int argc, char **argv)
{
    int Ret = EXIT_DONT_CONTINUE;
    char console[50];
    unsigned int Retries;
    unsigned int Stage;
    Machine * TestMachine = NULL;

    SysregPrintf("sysreg2 %s starting\n", gGitCommit);

    /* Get the output path to the built ReactOS files */
    OutputPath = getenv("ROS_OUTPUT");
    if(!OutputPath)
        OutputPath = DefaultOutputPath;

    SysregPrintf("(Debug) Before calling InitializeModuleList().\n");
    InitializeModuleList();
    SysregPrintf("(Debug) After calling InitializeModuleList().\n");

    if (!LoadSettings(argc > 1 ? argv[1] : "sysreg.xml"))
    {
        SysregPrintf("Cannot load configuration file\n");
        goto cleanup;
    }

    /* Allocate proper machine */
    switch (AppSettings.VMType)
    {
        case TYPE_KVM:
            SysregPrintf("Info: AppSettings.VMType = TYPE_KVM\n");
            TestMachine = new KVM();
            break;
        case TYPE_VMWARE_PLAYER:
            SysregPrintf("Info: AppSettings.VMType = TYPE_VMWARE_PLAYER\n");
            TestMachine = new VMWarePlayer();
            break;
        case TYPE_VIRTUALBOX:
            SysregPrintf("Info: AppSettings.VMType = TYPE_VIRTUALBOX\n");
            TestMachine = new VirtualBox();
            break;
    }

    /* Don't go any further if connection failed */
    // SysregPrintf("(Debug) Before calling IsConnected().\n");
    if (!TestMachine->IsConnected())
    {
        SysregPrintf("Error: failed to connect to test machine.\n");
        goto cleanup;
    }
    // SysregPrintf("(Debug) After calling IsConnected().\n");

    /* Shutdown the machine if already running */
    SysregPrintf("(Debug) Before calling IsMachineRunning().\n");
    // Car par encore démarrée.
    // "libvirt: QEMU Driver error : Domain not found: no domain with matching name 'ReactOS_AppVeyor-QEMU'".
    if (TestMachine->IsMachineRunning(AppSettings.Name, true))
    {
        SysregPrintf("Error: Test Machine is still running.\n");
        goto cleanup;
    }
    SysregPrintf("(Debug) After calling IsMachineRunning().\n");

    /* Initialize disk if needed */
    // SysregPrintf("(Debug) Before calling InitializeDisk().\n");
    TestMachine->InitializeDisk();
    // SysregPrintf("(Debug) After calling InitializeDisk().\n");

    for(Stage = 0; Stage < NUM_STAGES; Stage++)
    {
        printf("\n");
        SysregPrintf("Starting stage %u\n", Stage + 1);

        /* Execute hook command before stage if any */
        if (AppSettings.Stage[Stage].HookCommand[0] != '\0')
        {
            SysregPrintf("Applying hook: %s\n", AppSettings.Stage[Stage].HookCommand);
            int out = Execute(AppSettings.Stage[Stage].HookCommand);
            if (out < 0)
            {
                SysregPrintf("Hook command failed!\n");
                goto cleanup;
            }
        }

        for (Retries = 0; Retries <= AppSettings.MaxRetries; Retries++)
        {
            struct timeval StartTime, EndTime, ElapsedTime;

            printf("\n");
            if (Retries == 0)
            {
                SysregPrintf("Running stage %u...\n", Stage + 1);
            }
            else
            {
                SysregPrintf("Running stage %u retry %u...\n", Stage + 1, Retries);
            }

            if (!TestMachine->LaunchMachine(AppSettings.Filename,
                                            AppSettings.Stage[Stage].BootDevice))
            {
                SysregPrintf("LaunchMachine failed!\n");
                goto cleanup;
            }

            SysregPrintf("LibVirt domain %s started.\n", TestMachine->GetMachineName());

            gettimeofday(&StartTime, NULL);

            // SysregPrintf("(Debug) Before calling GetConsole().\n");
            if (!TestMachine->GetConsole(console))
            {
                SysregPrintf("GetConsole failed!\n");
                goto cleanup;
            }
            // SysregPrintf("(Debug) After calling GetConsole().\n");

            printf("\n");
            Ret = ProcessDebugData(console, AppSettings.Timeout, Stage);
            printf("\n");

            gettimeofday(&EndTime, NULL);

            SysregPrintf("(Debug) Before calling ShutdownMachine().\n");
            // Par exemple, fin de Stage 1 et 2, car arrêt de ReactOS lui-même.
            // "libvirt: QEMU Driver error : Requested operation is not valid: domain is not running".
            TestMachine->ShutdownMachine();
            SysregPrintf("(Debug) After calling ShutdownMachine().\n");

            timersub(&EndTime, &StartTime, &ElapsedTime);
            if (Retries == 0)
            {
                SysregPrintf("Stage %u took: %ld.%06ld seconds\n",
                             Stage + 1, ElapsedTime.tv_sec, ElapsedTime.tv_usec);
            }
            else
            {
                SysregPrintf("Stage %u retry %u took: %ld.%06ld seconds\n",
                             Stage + 1, Retries, ElapsedTime.tv_sec, ElapsedTime.tv_usec);
            }

            usleep(1000);

            /* If we have a checkpoint to reach for success, assume that
               the application used for running the tests (probably "rosautotest")
               continues with the next test after a VM restart. */
            if (Ret != EXIT_CONTINUE || !*AppSettings.Stage[Stage].Checkpoint)
            {
                break;
            }
        }

        if (Retries > AppSettings.MaxRetries)
        {
            printf("\n");
            SysregPrintf("Maximum number (%u) of retries reached, aborting!\n",
                         AppSettings.MaxRetries);
            break;
        }

        if (Ret == EXIT_DONT_CONTINUE)
            break;
    }


cleanup:
    xmlCleanupParser();

    CleanModuleList();

    printf("\n");
    switch (Ret)
    {
        case EXIT_CHECKPOINT_REACHED:
            SysregPrintf("Status: Reached the checkpoint.\n");
            break;
        case EXIT_CONTINUE:
            SysregPrintf("Status: Failed to reach the checkpoint!\n");
            break;
        case EXIT_DONT_CONTINUE:
            SysregPrintf("Status: Testing process aborted!\n");
            break;
    }

    delete TestMachine;

    /* Add a blank line, as a separator in case, i.e., the output is redirected to 'log2lines -s' */
    printf("\n");
    return Ret;
}
