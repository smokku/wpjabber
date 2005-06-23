/* Create the Jabber database */

/* $Id: sample_database.sql,v 1.7 2003/04/29 18:18:46 bobeal Exp $ */

DROP DATABASE IF EXISTS jabber;
CREATE DATABASE jabber;
USE jabber;

/* User information table - authentication information and profile */
CREATE TABLE users (
  username VARCHAR(64) NOT NULL PRIMARY KEY,
  password VARCHAR(16) NOT NULL,
  INDEX login (username, password)
);

CREATE TABLE usershash (
  username VARCHAR(64) NOT NULL PRIMARY KEY,
  authhash VARCHAR(34) NOT NULL
);

CREATE TABLE users0k (
  username VARCHAR(64) NOT NULL PRIMARY KEY,
  hash     VARCHAR(41) NOT NULL,
  token    VARCHAR(10) NOT NULL,
  sequence VARCHAR(3)  NOT NULL
);

CREATE TABLE last (
  username VARCHAR(64) NOT NULL PRIMARY KEY,
  seconds  VARCHAR(32) NOT NULL,
  state	   VARCHAR(32)
);

/* User resource mappings */
CREATE TABLE userres (
  username VARCHAR(64) NOT NULL,
  resource VARCHAR(32) NOT NULL,
  PRIMARY KEY (username, resource)
);

/* Roster listing */
CREATE TABLE rosterusers (
  username VARCHAR(64) NOT NULL,
  jid VARCHAR(64) NOT NULL,
  nick VARCHAR(255),
  subscription CHAR(1) NOT NULL,  /* 'N', 'T', 'F', or 'B' */
  ask CHAR(1) NOT NULL,           /* '-', 'S', or 'U' */
  server CHAR(1) NOT NULL,        /* 'Y' or 'N' */
  subscribe VARCHAR(255),
  type VARCHAR(64),               /* item for normal jids
                                   * public or private for confs bookmarks */
  INDEX rostuser (username)
);


CREATE TABLE rostergroups (
  username VARCHAR(64) NOT NULL,
  jid VARCHAR(64) NOT NULL,
  grp VARCHAR(64) NOT NULL
);

/* Spooled offline messages */
CREATE TABLE spool (
  username VARCHAR(64) NOT NULL,
  receiver VARCHAR(255) NOT NULL,
  sender VARCHAR(255) NOT NULL,                 
  id VARCHAR(255),
  date TIMESTAMP,
  priority INT,
  type VARCHAR(32),
  thread VARCHAR(255),
  subject VARCHAR(255),
  message TEXT,
  extension TEXT,
  INDEX despool (username, date)
);

CREATE TABLE route (
  username VARCHAR(64) NOT NULL,
  resource VARCHAR(32) NOT NULL,
  location VARCHAR(255) NOT NULL
);

CREATE TABLE filters (
  username     VARCHAR(64),
  unavailable  VARCHAR(1),
  sender       VARCHAR(255),
  resource     VARCHAR(32),
  subject      VARCHAR(255),
  body         TEXT,
  show_state   VARCHAR(8),
  type         VARCHAR(8),
  offline      VARCHAR(1),
  forward      VARCHAR(32),
  reply        TEXT,
  continue     VARCHAR(1),
  settype      VARCHAR(8)
);

CREATE TABLE vcard (
  username   VARCHAR(64) PRIMARY KEY,
  full_name  VARCHAR(65),
  first_name VARCHAR(32),
  last_name  VARCHAR(32),
  nick_name  VARCHAR(32),
  url        VARCHAR(255),
  address1   VARCHAR(255),
  address2   VARCHAR(255),
  locality   VARCHAR(32),
  region     VARCHAR(32),
  pcode      VARCHAR(32),
  country    VARCHAR(32),
  telephone  VARCHAR(32),
  email      VARCHAR(127),
  orgname    VARCHAR(32),
  orgunit    VARCHAR(32),
  title      VARCHAR(32),
  role       VARCHAR(32),
  b_day      DATE,
  descr      TEXT
);

CREATE TABLE yahoo (
  username   VARCHAR(128) PRIMARY KEY,
  id         VARCHAR(100) NOT NULL,
  pass       VARCHAR(32) NOT NULL
);

CREATE TABLE icq (
  username   VARCHAR(128) PRIMARY KEY,
  id         VARCHAR(100) NOT NULL,
  pass       VARCHAR(32) NOT NULL
);

CREATE TABLE aim (
  username   VARCHAR(128) PRIMARY KEY,
  id         VARCHAR(100) NOT NULL,
  pass       VARCHAR(32) NOT NULL
);

/* muc tables */
create table rooms (
  room varchar(64) not null,
  roomid varchar(96) not null,
  primary key (room, roomid)
);

create table roomconfig (
  roomid varchar(96) not null,
  owner varchar(64),
  name varchar(64),
  secret varchar(64),
  private char(1),
  leave_msg varchar(64),
  join_msg varchar(64),
  rename_msg varchar(64),
  public char(1),
  persistant char(1),
  subjectlock char(1),
  maxusers varchar(10),
  moderated char(1),
  defaultype char(1),
  privmsg char(1),
  invitation char(1),
  invites char(1),
  legacy char(1),
  visible char(1),
  logformat char(1),
  logging char(1),
  description varchar(128),
  primary key (roomid)
);

create table roomadmin (
  roomid varchar(96) not null,
  userid varchar(64) not null,
  primary key (roomid, userid)
);

create table roommember (
  roomid varchar(96) not null,
  userid varchar(64) not null,
  primary key (roomid, userid)
);

create table roomoutcast (
  roomid varchar(96) not null,
  userid varchar(64) not null,
  reason varchar(64),
  responsibleid varchar(64),
  responsiblenick varchar(64),
  primary key (roomid, userid)
);

create table roomregistration (
  nick varchar(64),
  keynick varchar(64),
  id varchar(64) not null,
  conference varchar(64) not null,
  primary key (id, conference)
);


/* Grant privileges to some users */
GRANT SELECT, INSERT, UPDATE, DELETE ON jabber.* TO jabber@localhost
    IDENTIFIED BY 'secret';
