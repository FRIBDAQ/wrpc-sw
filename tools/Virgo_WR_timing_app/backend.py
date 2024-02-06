from pysnmp.smi import builder, view, error
from pysnmp.hlapi import *
import os

class SnmpHandler():

    snmpPort = 161
    wrpcCommunityString = 'public'
    group = "Group"
    mibLeaves = {}

    def __init__(self, targetIp=None, wrpcMibName=None, mibDir=None, coreOid=None):
        self.coreOid = coreOid
        self.mibDir = os.path.expanduser(mibDir)
        self.wrpcMibName = wrpcMibName
        self.targetIp = targetIp
        self.controller = self.create_MibController()
        self.mibLeaves = self.get_local_info()

    def create_MibController(self):
        try:
            mibBuilder = builder.MibBuilder() 
            mibPath = mibBuilder.getMibSources()+(builder.DirMibSource(self.mibDir),)
            mibBuilder.setMibSources(*mibPath)
            mibBuilder.loadModules(self.wrpcMibName)
            mibController = view.MibViewController(mibBuilder)

        except Exception as e:
            print(f"An error occurred: {e}")
            return None
 
        return mibController

    def print_local_info(self):
        for key, value in self.mibLeaves.items():
            print(f"{key} : {value[0]}")

    def walk(self):
        for key, _ in self.mibLeaves.items():
            self.get(key)

    def get(self, name):
        
        if name in self.mibLeaves:
            flag = self.mibLeaves[name][1]
        else:
            print("Invalid name was entered")
            return

        if flag:
            object = ObjectType(ObjectIdentity(self.wrpcMibName, name, 0))
        else:
            object = ObjectType(ObjectIdentity(self.wrpcMibName, name))

        try:
            _, _, _, varBinds = next(
                getCmd(SnmpEngine(),
                    CommunityData(self.wrpcCommunityString),
                    UdpTransportTarget((self.targetIp, self.snmpPort)),
                    ContextData(),
                    object))

        except error.SmiError as e:
            print(f"An error occured during the get operation. {e}")
            return

        return varBinds

    def set(self, name, value):

        if name in self.mibLeaves:
            flag = self.mibLeaves[name][1]
        else:
            print("Invalid name was entered")
            return

        if flag:
            object = ObjectType(ObjectIdentity(self.wrpcMibName, name, 0), value)
        else:
            object = ObjectType(ObjectIdentity(self.wrpcMibName, name), value)

        try:
            next(
                setCmd(SnmpEngine(),
                    CommunityData(self.wrpcCommunityString),
                    UdpTransportTarget((self.targetIp, self.snmpPort)),
                    ContextData(),
                    object))

        except error.SmiError as e:
            print(f"An error occured during the set operation. {e}")
            return

    def get_local_info(self):

        leaves = {}
        labels_list = []
        nodes = []
        oid, _, _ = self.controller.getNodeName(self.coreOid)
        modName, _, _ = self.controller.getNodeLocation(self.coreOid)
        table_flag = 0

        while True:
            try:        
                oid, labels, _ = self.controller.getNextNodeName(oid)
                modName, _, _ = self.controller.getNodeLocation(oid)
                # gather only white rabbit related MIB variables
                if modName == self.wrpcMibName:
                    labels_list.append((labels))
                    table_flag = any(self.group in label for label in labels)
                    nodes.append((labels[-1], oid, table_flag))

                table_flag = 0
            
            except Exception as e:
                print(f"An exception occured during retrieving information from local MIB. {e}")
                break

        # the name of every leaf must appear only once
        occurrences = 0
        for (name, oid, table_flag) in nodes:
            for label in labels_list:
                occurrences += label.count(name)
            if occurrences == 1:
                leaves[name] = (oid, table_flag)
            occurrences = 0
        
        return leaves