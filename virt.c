/*
 * PROJECT:     ReactOS System Regression Testing Utility
 * LICENSE:     GNU GPLv2 or any later version as published by the Free Software Foundation
 * PURPOSE:     Main entry point and controlling the virtual machine
 * COPYRIGHT:   Copyright 2008-2009 Christoph von Wittich <christoph_vw@reactos.org>
 *              Copyright 2009 Colin Finck <colin@reactos.org>
 */

#include "sysreg.h"

const char DefaultOutputPath[] = "output-i386";
const char* OutputPath;
Settings AppSettings;
ModuleListEntry* ModuleList;

static int AuthCreds[] = {
    VIR_CRED_PASSPHRASE,
};

bool GetConsole(virDomainPtr vDomPtr, char* console)
{
    xmlDocPtr xml = NULL;
    xmlXPathObjectPtr obj = NULL;
    xmlXPathContextPtr ctxt = NULL;
    char* XmlDoc;
    bool RetVal = false;

    XmlDoc = virDomainGetXMLDesc(vDomPtr, 0);
    if (!XmlDoc)
        return false;

    xml = xmlReadDoc((const xmlChar *) XmlDoc, "domain.xml", NULL,
            XML_PARSE_NOENT | XML_PARSE_NONET |
            XML_PARSE_NOWARNING);
    free(XmlDoc);
    if (!xml)
        return false;

    ctxt = xmlXPathNewContext(xml);
    if (!ctxt)
    {
        xmlFreeDoc(xml);
        return false;
    }

    obj = xmlXPathEval(BAD_CAST "string(/domain/devices/console/@tty)", ctxt);
    if ((obj != NULL) && ((obj->type == XPATH_STRING) &&
                         (obj->stringval != NULL) && (obj->stringval[0] != 0)))
    {
        strcpy(console, obj->stringval);
        RetVal = true;
    }
    if (obj)
        xmlXPathFreeObject(obj);

    xmlFreeDoc(xml);
    xmlXPathFreeContext(ctxt);
    return RetVal;
}

bool IsVirtualMachineRunning(virConnectPtr vConn, const char* name)
{
    int* ids = NULL;
    int numids;
    int maxids = 0;
    const char* domname;
    virDomainPtr vDomPtr = NULL;

    maxids = virConnectNumOfDomains(vConn);
    if (maxids < 0)
        return false;

    ids = malloc(sizeof(int) * maxids);
    if (!ids)
        return false;

    numids = virConnectListDomains(vConn, &ids[0], maxids);
    if (numids > -1)
    {
        int i;
        for(i=0; i<numids; i++)
        {
            vDomPtr = virDomainLookupByID(vConn, ids[i]);
            domname = virDomainGetName(vDomPtr);
            if (strcasecmp(name, domname) == 0)
            {
                virDomainFree(vDomPtr);
                free(ids);
                return true;
            }
            virDomainFree(vDomPtr);
        }
    }
    free(ids);
    return false;
}

virDomainPtr LaunchVirtualMachine(virConnectPtr vConn, const char* XmlFileName, const char* BootDevice)
{
    xmlDocPtr xml = NULL;
    xmlXPathObjectPtr obj = NULL;
    xmlXPathContextPtr ctxt = NULL;
    char* XmlDoc;
    char* buffer;
    const char* name;
    char* domname;
    int len = 0;

    buffer = ReadFile(XmlFileName);
    if (buffer == NULL)
        return NULL;

    xml = xmlReadDoc((const xmlChar *) buffer, "domain.xml", NULL,
                      XML_PARSE_NOENT | XML_PARSE_NONET |
                      XML_PARSE_NOWARNING);
    if (!xml)
        return NULL;

    ctxt = xmlXPathNewContext(xml);
    if (!ctxt)
        return NULL;

    obj = xmlXPathEval(BAD_CAST "/domain/os/boot", ctxt);
    if ((obj != NULL) && (obj->type == XPATH_NODESET)
            && (obj->nodesetval != NULL) && (obj->nodesetval->nodeTab != NULL))
    {
        xmlSetProp(obj->nodesetval->nodeTab[0], "dev", BootDevice);
    }
    if (obj)
        xmlXPathFreeObject(obj);

    free(buffer);
    xmlDocDumpMemory(xml, (xmlChar**) &buffer, &len);
    xmlFreeDoc(xml);
    xmlXPathFreeContext(ctxt);

    virDomainPtr vDomPtr = virDomainDefineXML(vConn, buffer);
    xmlFree((xmlChar*)buffer);
    if (vDomPtr)
    {
        if (virDomainCreate(vDomPtr) != 0)
        {
            virDomainUndefine(vDomPtr);
            virDomainFree(vDomPtr);
            vDomPtr = NULL;
        }
        else
        {
            /* workaround a bug in libvirt */
            name = virDomainGetName(vDomPtr);
            domname = strdup(name);
            virDomainFree(vDomPtr);
            vDomPtr = virDomainLookupByName(vConn, domname);
            free(domname);
        }
    }
    return vDomPtr;
}

int AuthToVMware(virConnectCredentialPtr cred, unsigned int ncred, void *cbdata)
{
    int i;

    for (i = 0; i < ncred; i++)
    {
        if (cred[i].type == VIR_CRED_PASSPHRASE)
        {
            cred[i].result = strdup(AppSettings.Password);
            if (cred[i].result == NULL)
                return -1;
            cred[i].resultlen = strlen(cred[i].result);
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    virConnectPtr vConn = NULL;
    virDomainPtr vDom;
    virDomainInfo info;
    char qemu_img_cmdline[300];
    FILE* file;
    char config[255];
    int Ret = EXIT_DONT_CONTINUE;
    char console[50];
    unsigned int Retries;
    unsigned int Stage;
    char libvirt_cmdline[300];
    virConnectAuth vAuth;

    /* Get the output path to the built ReactOS files */
    OutputPath = getenv("ROS_OUTPUT");
    if(!OutputPath)
        OutputPath = DefaultOutputPath;

    InitializeModuleList();

    if (argc == 2)
        strcpy(config, argv[1]);
    else
        strcpy(config, "sysreg.xml");

    if (!LoadSettings(config))
    {
        SysregPrintf("Cannot load configuration file\n");
        goto cleanup;
    }

    switch (AppSettings.VMType)
    {
        case TYPE_KVM:
            vConn = virConnectOpen("qemu:///session");
            break;

        case TYPE_VMWARE_PLAYER:
            vConn = virConnectOpen("vmwareplayer:///session");
            break;

        case TYPE_VMWARE_GSX:
        case TYPE_VMWARE_ESX:
            if (AppSettings.VMType == TYPE_VMWARE_GSX)
                strcpy(libvirt_cmdline, "gsx://");
            else
                strcpy(libvirt_cmdline, "esx://");

            if (AppSettings.Username)
            {
                strcat(libvirt_cmdline, AppSettings.Username);
                strcat(libvirt_cmdline, "@");
            }

            if (AppSettings.Domain)
                strcat(libvirt_cmdline, AppSettings.Domain);
            else
                strcat(libvirt_cmdline, "localhost");

            if (AppSettings.Password)
            {
                vAuth.credtype = AuthCreds;
                vAuth.ncredtype = sizeof(AuthCreds)/sizeof(int);
                vAuth.cb = AuthToVMware;
                vAuth.cbdata = NULL;
            }
            else
                vAuth = *virConnectAuthPtrDefault;

            vConn = virConnectOpenAuth(libvirt_cmdline, &vAuth, 0);
            break;
    }

    if (IsVirtualMachineRunning(vConn, AppSettings.Name))
    {
        /* SysregPrintf("Error: Virtual Machine is already running.\n");
        goto cleanup; */
        system("virsh destroy ReactOS");
        usleep(1000);
    }

    /* If the HD image already exists, delete it */
    if (file = fopen(AppSettings.HardDiskImage, "r"))
    {
        fclose(file);
        remove(AppSettings.HardDiskImage);
    }

    /* Create a new HD image */
    if (AppSettings.VMType == TYPE_KVM)
    {
        sprintf(qemu_img_cmdline, "qemu-img create -f qcow2 %s %dM",
                AppSettings.HardDiskImage, AppSettings.ImageSize);
    }
    else if (AppSettings.VMType == TYPE_VMWARE_PLAYER ||
             AppSettings.VMType == TYPE_VMWARE_GSX)
    {
        sprintf(qemu_img_cmdline, "qemu-img create -f vmdk %s %dM",
                AppSettings.HardDiskImage, AppSettings.ImageSize);
    }

    FILE* p = popen(qemu_img_cmdline, "r");
    char buf[100];
    while(feof(p)==0)
    {
        fgets(buf,100,p);
        SysregPrintf("%s\n",buf);
    }
    pclose(p);

    for(Stage = 0; Stage < NUM_STAGES; Stage++)
    {
        for(Retries = 0; Retries < AppSettings.MaxRetries; Retries++)
        {
            vDom = LaunchVirtualMachine(vConn, AppSettings.Filename,
                    AppSettings.Stage[Stage].BootDevice);

            if (!vDom)
            {
                SysregPrintf("LaunchVirtualMachine failed!\n");
                goto cleanup;
            }

            printf("\n\n\n");
            SysregPrintf("Running stage %d...\n", Stage + 1);
            SysregPrintf("Domain %s started.\n", virDomainGetName(vDom));

            GetConsole(vDom, console);
            Ret = ProcessDebugData(console, AppSettings.Timeout, Stage);

            /* Kill the VM */
            virDomainGetInfo(vDom, &info);

            if (info.state != VIR_DOMAIN_SHUTOFF)
                virDomainDestroy(vDom);

            virDomainUndefine(vDom);
            virDomainFree(vDom);

            usleep(1000);

            /* If we have a checkpoint to reach for success, assume that
               the application used for running the tests (probably "rosautotest")
               continues with the next test after a VM restart. */
            if (Ret == EXIT_CONTINUE && *AppSettings.Stage[Stage].Checkpoint)
                SysregPrintf("Rebooting VM (retry %d)\n", Retries + 1);
            else
                break;
        }

        if (Retries == AppSettings.MaxRetries)
        {
            SysregPrintf("Maximum number of allowed retries exceeded, aborting!\n");
            break;
        }

        if (Ret == EXIT_DONT_CONTINUE)
            break;
    }


cleanup:
    CleanModuleList();

    if (vConn)
        virConnectClose(vConn);

    if (AppSettings.Domain)
        free(AppSettings.Domain);

    if (AppSettings.Username)
        free(AppSettings.Username);

    if (AppSettings.Password)
        free(AppSettings.Password);

    switch (Ret)
    {
        case EXIT_CHECKPOINT_REACHED:
            SysregPrintf("Status: Reached the checkpoint!\n");
            break;

        case EXIT_CONTINUE:
            SysregPrintf("Status: Failed to reach the checkpoint!\n");
            break;

        case EXIT_DONT_CONTINUE:
            SysregPrintf("Status: Testing process aborted!\n");
            break;
    }

    return Ret;
}
