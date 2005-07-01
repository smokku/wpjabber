/* Create the Jabber database */

/* $Id: sample_database.pg.sql,v 1.8 2003/04/26 17:17:59 bobeal Exp $ */

/* User information table - authentication information and profile */
CREATE TABLE users (
  username VARCHAR(2048) NOT NULL PRIMARY KEY,
  password VARCHAR(64) NOT NULL
);
CREATE INDEX I_users_login ON users (username, password);

CREATE TABLE usershash (
  username VARCHAR(2048) NOT NULL PRIMARY KEY,
  authhash VARCHAR(34) NOT NULL
);

CREATE TABLE last (
  username VARCHAR(2048) NOT NULL PRIMARY KEY,
  seconds  VARCHAR(32) NOT NULL,
  state	   VARCHAR(256)
);

/* User registration data */
CREATE TABLE registered (
  username   VARCHAR(2048) NOT NULL PRIMARY KEY,
  login      VARCHAR(1024),
  password   VARCHAR(64) NOT NULL,
  full_name  VARCHAR(256),
  email      VARCHAR(127),
  stamp      VARCHAR(32),
  type       VARCHAR(32)
);

/* Roster listing */
CREATE TABLE rosterusers (
  username VARCHAR(2048) NOT NULL,
  jid VARCHAR(2048) NOT NULL,
  nick VARCHAR(1024),
  subscription CHAR(1) NOT NULL,  /* 'N', 'T', 'F', or 'B' */
  ask CHAR(1) NOT NULL,           /* '-', 'S', or 'U' */
  subscribe VARCHAR(255),
  type VARCHAR(64),
  extension TEXT
);
CREATE INDEX I_rosteru_username ON rosterusers (username);

CREATE TABLE rostergroups
(
  username VARCHAR(2048) NOT NULL,
  jid VARCHAR(2048) NOT NULL,
  grp VARCHAR(1024) NOT NULL
);

/* Spooled offline messages */
CREATE TABLE spool (
  username VARCHAR(2048) NOT NULL,
  receiver VARCHAR(2048) NOT NULL,
  sender VARCHAR(2048) NOT NULL,                 
  id VARCHAR(255),
  date VARCHAR(32),
  priority VARCHAR(8),
  type VARCHAR(32),
  thread VARCHAR(1024),
  subject VARCHAR(1024),
  message TEXT,
  extension TEXT
);
CREATE INDEX I_despool on spool (username, date);

CREATE TABLE filters (
  username     VARCHAR(2048),
  unavailable  VARCHAR(1),
  sender       VARCHAR(2048),
  resource     VARCHAR(1024),
  subject      VARCHAR(1024),
  body         TEXT,
  show_state   VARCHAR(8),
  type         VARCHAR(8),
  offline      VARCHAR(1),
  forward      VARCHAR(2048),
  reply        TEXT,
  continue     VARCHAR(1),
  settype      VARCHAR(8)
);

CREATE TABLE vcard (
  username   VARCHAR(2048) PRIMARY KEY,
  full_name  VARCHAR(256),
  first_name VARCHAR(64),
  last_name  VARCHAR(64),
  nick_name  VARCHAR(64),
  url        VARCHAR(512),
  address1   VARCHAR(256),
  address2   VARCHAR(256),
  locality   VARCHAR(64),
  region     VARCHAR(64),
  pcode      VARCHAR(32),
  country    VARCHAR(64),
  telephone  VARCHAR(32),
  email      VARCHAR(127),
  orgname    VARCHAR(64),
  orgunit    VARCHAR(64),
  title      VARCHAR(32),
  role       VARCHAR(32),
  b_day      VARCHAR(32),
  descr      TEXT,
  photo_type VARCHAR(32),
  photo_bin  TEXT
);
CREATE INDEX I_vcard_username ON vcard (username);

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

CREATE TABLE msn (
  username   VARCHAR(128) PRIMARY KEY,
  id         VARCHAR(100) NOT NULL,
  pass       VARCHAR(32) NOT NULL
);

/* muc tables */
create table rooms (
  room varchar(64) not null,
  roomid varchar(96) not null
);
CREATE UNIQUE INDEX PK_rooms ON rooms (room, roomid);

create table roomconfig (
  roomid varchar(96) not null,
  owner varchar(64),
  name varchar(64),
  secret varchar(64),
  private char(1),
  leave varchar(64),
  "join" varchar(64),
  rename varchar(64),
  "public" char(1),
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
  description varchar(128)
);
CREATE UNIQUE INDEX PK_roomconfig ON roomconfig (roomid);

create table roomadmin (
  roomid varchar(96) not null,
  userid varchar(64) not null
);
CREATE UNIQUE INDEX PK_roomadmin ON roomadmin (roomid, userid);

create table roommember (
  roomid varchar(96) not null,
  userid varchar(64) not null
);
CREATE UNIQUE INDEX PK_roommember ON roommember (roomid, userid);

create table roomoutcast (
  roomid varchar(96) not null,
  userid varchar(64) not null,
  reason varchar(64),
  responsibleid varchar(64),
  responsiblenick varchar(64)
);
CREATE UNIQUE INDEX PK_roomoutcast ON roomoutcast (roomid, userid);

create table roomregistration (
  nick varchar(64),
  keynick varchar(64),
  id varchar(64) not null,
  conference varchar(64) not null
);
CREATE UNIQUE INDEX PK_roomregistration ON roomregistration (id, conference);


alter table last add constraint last_user
    foreign key(username) references users(username) on delete cascade;

alter table userres add constraint userres_user
    foreign key(username) references users(username) on delete cascade;

alter table rosterusers add constraint rosterusers_user
    foreign key(username) references users(username) on delete cascade;

alter table rostergroups add constraint rostergroups_user
    foreign key(username) references users(username) on delete cascade;

alter table spool add constraint spool_user
    foreign key(username) references users(username) on delete cascade;

alter table filters add constraint filters_user
    foreign key(username) references users(username) on delete cascade;

alter table vcard add constraint vcard_user
    foreign key(username) references users(username) on delete cascade;

alter table yahoo add constraint yahoo_user
    foreign key(username) references users(username) on delete cascade;

alter table icq add constraint icq_user
    foreign key(username) references users(username) on delete cascade;

alter table aim add constraint aim_user
    foreign key(username) references users(username) on delete cascade;

/* Grant privileges to some users */
GRANT ALL ON users, last, userres, rosterusers, rostergroups, spool, filters, vcard, yahoo, icq, aim, rooms, roomadmin , roommember, roomoutcast, roomregistration TO jabber;
