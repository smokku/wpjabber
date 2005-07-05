#!/usr/bin/python -u
#
#  xdb_copy utility
#  Copyright (C) 2005  Tomasz Sterna <smoku@chrome.pl>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.


import sys
import libxml2
import pyxmpp.jabberd
from pyxmpp import JID
from pyxmpp import StreamError,FatalStreamError
from pyxmpp.jabberd.component import Component
import os
import codecs

class FatalError(RuntimeError):
    pass

class ConnectionConfig:
    def __init__(self,node):
        self.node=node
        self.host=node.xpathEval("host")[0].getContent()
        self.port=int(node.xpathEval("port")[0].getContent())
        self.secret=node.xpathEval("secret")[0].getContent()
        self.jid=JID(node.xpathEval("jid")[0].getContent())

class NSConfig:
    def __init__(self,node):
        self.node=node
	self.namespaces=[]
        for ns in node.xpathEval("ns"):
	    self.namespaces.append(ns.getContent())
	
class Config:
    def __init__(self,config_file):
        self.doc=None
        self.config_file=config_file
        parser=libxml2.createFileParserCtxt(os.path.join(config_file))
        parser.validate(0)
        parser.parseDocument()
        if not parser.isValid():
            raise FatalError,"Invalid configuration"
        self.doc=parser.doc()
        self.source=ConnectionConfig(self.doc.xpathEval("xdbcopy/source")[0])
        self.destination=ConnectionConfig(self.doc.xpathEval("xdbcopy/destination")[0])
	self.ns=NSConfig(self.doc.xpathEval("xdbcopy/xdbns")[0])
    def __del__(self):
        if self.doc:
            self.doc.freeDoc()


def process_node(node):
    global dest
    global config
    
    node.setProp("type","set")
    node.setProp("to",node.prop("from"))
    node.setProp("from",config.destination.jid.domain)
    node.unsetProp("xmlns")
    dest.stream._write_node(node)

def authenticated():
    global source
    global dest
    global config
    global users
    global lastuser

    source.get_stream()._process_node=process_node
    i = 1
    doc_out=libxml2.newDoc("1.0")
    for user in users:
        user=user.strip()
        print "Copy: "+user.encode('utf-8')
	lastuser=user
        for ns in config.ns.namespaces:
            node=doc_out.newChild(None, "xdb", None)
            node.setProp("type","get")
            node.setProp("to",user.encode('utf-8'))
            node.setProp("from",config.source.jid.domain)
            node.setProp("ns",ns)
            node.setProp("id",str(i))
            source.stream._write_node(node)
            i=i+1

def auth_dest():
    global dest
    dest.get_stream()._process_node=dest_node

def dest_node(node):
    global shutdown
    global lastuser
    global config
    
    if node.prop("type")=="result":
        result="OK"
    else:
        result="ERROR"
    print node.prop("from").encode('utf-8')+":"+node.prop("ns")+"... "+result
    if node.prop("from")==lastuser and node.prop("ns")==config.ns.namespaces[len(config.ns.namespaces)-1]:
        shutdown="OK"

def main():
    global source
    global config
    global dest
    global users
    global shutdown

    config_file="xdb_copy.xml"
    user_file="xdb_copy.users"

    try:
        try:
            config=Config(config_file)
        except:
            print >>sys.stderr,"Couldn't load config file:",str(sys.exc_value)
            sys.exit(1)
	try:
	    userfile=codecs.open(user_file, "r", "utf-8")
        except:
            print >>sys.stderr,"Couldn't load users file:",str(sys.exc_value)
            sys.exit(1)
	
	users=userfile.readlines()
	source=Component(jid=config.source.jid,server=config.source.host,port=config.source.port,secret=config.source.secret);
	source.authenticated=authenticated
        source.connect()
	dest=Component(jid=config.destination.jid,server=config.destination.host,port=config.destination.port,secret=config.destination.secret);
	dest.authenticated=auth_dest
        dest.connect()
	shutdown=None
        try:
            while (not shutdown and source.stream and not source.stream.eof and source.stream.socket is not None):
                try:
                    source.stream.loop_iter(1)
                    dest.stream.loop_iter(1)
		    
                except (KeyboardInterrupt,SystemExit,FatalStreamError,StreamError):
                    raise
                except:
                    source.print_exception()
        finally:
            source.disconnect()
            source.debug("Exitting normally")
	

    except FatalError,e:
        print e
        print "Aborting."
        sys.exit(1)

main()

# vi: sts=4 et sw=4
