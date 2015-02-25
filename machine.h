/*
 * PROJECT:     ReactOS System Regression Testing Utility
 * LICENSE:     GNU GPLv2 or any later version as published by the Free Software Foundation
 * PURPOSE:     Shared C++ header
 * COPYRIGHT:   Copyright 2013 Pierre Schweitzer <pierre@reactos.org>
 */

#ifndef __MACHINE_H__
#define __MACHINE_H__

#include "sysreg.h"
#include <new>

class Machine
{
public:
    Machine() {};

    virtual bool IsMachineRunning(const char * name, bool destroy) = 0;
    virtual void InitializeDisk() = 0;
    virtual bool LaunchMachine(const char* XmlFileName, const char* BootDevice) = 0;
    virtual const char * GetMachineName() const = 0;
    virtual bool GetConsole(char* console) = 0;
    virtual void ShutdownMachine() = 0;

    virtual ~Machine() {};
};

class LibVirt : public Machine
{
public:
    LibVirt();
    virtual ~LibVirt();

    virtual bool IsMachineRunning(const char * name, bool destroy);
    virtual void InitializeDisk();
    virtual bool LaunchMachine(const char* XmlFileName, const char* BootDevice);
    virtual const char * GetMachineName() const;
    virtual void ShutdownMachine();

protected:
    virConnectPtr vConn;
    virDomainPtr vDom;
};

class KVM : public LibVirt
{
public:
    KVM();

    virtual bool GetConsole(char* console);
};

class VMWarePlayer : public LibVirt
{
public:
    VMWarePlayer();

    virtual bool GetConsole(char* console);
    bool StartListeningSocket(void);
};

class VMWareESX : public LibVirt
{
public:
    VMWareESX();

    virtual bool GetConsole(char* console);

    static int AuthToVMware(virConnectCredentialPtr cred, unsigned int ncred, void *cbdata);
};

class VirtualBox : public LibVirt
{
public:
    VirtualBox();

    virtual bool GetConsole(char* console);
    virtual void InitializeDisk();
    bool StartListeningSocket(void);
};

#endif
