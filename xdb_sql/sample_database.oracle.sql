-- User information table - authentication information and profile 
CREATE TABLE users (
  username VARCHAR2(64) NOT NULL PRIMARY KEY,
  password VARCHAR2(16) NOT NULL
);

CREATE  INDEX login ON USERS (username, password) ;

CREATE TABLE usershash (
  username VARCHAR2(64) NOT NULL PRIMARY KEY,
  authhash VARCHAR2(34) NOT NULL
);

CREATE TABLE users0k (
  username VARCHAR2(32) NOT NULL PRIMARY KEY,
  hash     VARCHAR2(41) NOT NULL,
  token    VARCHAR2(10) NOT NULL,
  sequence VARCHAR2(3)  NOT NULL
);

CREATE TABLE last (
  username VARCHAR2(32) NOT NULL PRIMARY KEY,
  seconds  VARCHAR2(32) NOT NULL,
  state	   VARCHAR2(32)
);

-- User resource mappings 
CREATE TABLE userres (
  username VARCHAR2(32) NOT NULL,
  res VARCHAR2(32) NOT NULL,
  PRIMARY KEY (username, res)
);

-- Roster listing 
CREATE TABLE rosterusers (
  username VARCHAR2(32) NOT NULL,
  jid VARCHAR2(255) NOT NULL,
  nick VARCHAR2(255),
  subscription CHAR(1) NOT NULL,   --  'N', 'T', 'F', or 'B'  -- 
  ask CHAR(1) NOT NULL,            --  '-', 'S', or 'U'  -- 
  server CHAR(1) NOT NULL,         --  'Y' or 'N'  -- 
  subscribe VARCHAR2(10),
  type VARCHAR2(64)                --  item for normal jids
);

CREATE INDEX rostuser ON rosterusers (username);

CREATE TABLE rostergroups (
  username VARCHAR2(32) NOT NULL,
  jid VARCHAR2(223) NOT NULL,
  grp VARCHAR2(64) NOT NULL,
);

-- Spooled offline messages
CREATE TABLE spool (
  username VARCHAR2(32) NOT NULL,
  receiver VARCHAR2(255) NOT NULL,
  sender VARCHAR2(255) NOT NULL,                 
  id VARCHAR2(255),
  when DATE,
  priority INT,
  type VARCHAR2(32),
  thread VARCHAR2(255),
  subject VARCHAR2(255),
  message VARCHAR2(4000),
  extension VARCHAR2(4000)
);

CREATE  INDEX despool ON spool (username, when);

CREATE TABLE route (
  username VARCHAR2(32) NOT NULL,
  res      VARCHAR2(32) NOT NULL,
  location VARCHAR2(255) NOT NULL
);

CREATE TABLE filters (
  username     VARCHAR2(32),
  unavailable  VARCHAR2(1),
  sender       VARCHAR2(255),
  res          VARCHAR2(32),
  subject      VARCHAR2(255),
  body         VARCHAR2(4000),
  show_state   VARCHAR2(8),
  type         VARCHAR2(8),
  offl         VARCHAR2(1),
  forward      VARCHAR2(32),
  reply        VARCHAR2(4000),
  continue     VARCHAR2(1),
  settype      VARCHAR2(8)
);

CREATE TABLE vcard (
  username   VARCHAR2(32) PRIMARY KEY,
  full_name  VARCHAR2(65),
  first_name VARCHAR2(32),
  last_name  VARCHAR2(32),
  nick_name  VARCHAR2(32),
  url        VARCHAR2(255),
  address1   VARCHAR2(255),
  address2   VARCHAR2(255),
  locality   VARCHAR2(32),
  region     VARCHAR2(32),
  pcode      VARCHAR2(32),
  country    VARCHAR2(32),
  telephone  VARCHAR2(32),
  email      VARCHAR2(127),
  orgname    VARCHAR2(32),
  orgunit    VARCHAR2(32),
  title      VARCHAR2(32),
  role       VARCHAR2(32),
  b_day      DATE,
  descr      VARCHAR2(4000)
);

CREATE TABLE yahoo (
  username   VARCHAR2(128) PRIMARY KEY,
  id         VARCHAR2(100) NOT NULL,
  pass       VARCHAR2(32) NOT NULL
);

CREATE TABLE icq (
  username   VARCHAR2(128) PRIMARY KEY,
  id         VARCHAR2(100) NOT NULL,
  pass       VARCHAR2(32) NOT NULL
);

CREATE TABLE aim (
  username   VARCHAR2(128) PRIMARY KEY,
  id         VARCHAR2(100) NOT NULL,
  pass       VARCHAR2(32) NOT NULL
);

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
	leave varchar(64),
	join varchar(64),
	rename varchar(64),
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


alter table users0k add constraint users0k_user
    foreign key(username) references users(username) on delete cascade;

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
