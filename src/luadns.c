/*************************************************************************
*
* Copyright 2010 by Sean Conner.  All Rights Reserved.
*
* This library is free software; you can redistribute it and/or modify it
* under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 3 of the License, or (at your
* option) any later version.
*
* This library is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
* License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this library; if not, see <http://www.gnu.org/licenses/>.
*
**************************************************************************/

/**********************************************************************
*
* Implements Lua bindings for my DNS library.  This exports four functions
* in the org.conman.dns object:
*
*	encode(t)
*
*		Accepts a table in the form:
*
*			{
*			  id       = some_number,
*			  query    = true,	-- making a query
*			  rd       = true,	-- for recursive queries
*			  opcode   = 'query',
*			  question = {
*			  		name = 'www.example.com',
*			  		type = 'loc',
*			  		class = 'in'
*			  	}
*			  }
*
*		And returns a binary string that is the wire format of the
*		query.  This binary string can then be sent over a UDP or
*		TCP packet to a DNS server.
*
*		This returns a binary string on success, nil,rcode on
*		failre.
*
*		See lua/test.lua for an example of using this function.
*
*	decode(bs)
*
*		Decodes a binary string into a table (similar to the table
*		above) for easy use of the DNS response.
*
*		This returns a table on success, or nil,rcode on failure.
*
*		See lua/test.lua for an example of using this function.
*
*	query(server,bs)
*
*		Sends the encoded binary string to the given server.  The
*		server variable is a string of the IP address (IPv4 or
*		IPv6)---hostnames will fail.
*
*		This function is very stupid simple; it sends the request,
*		and if it doesn't see a reply in 15 seconds, it returns a
*		failure.  No restransmission of the packet is done.  This is
*		probably fine for simple applications but not for anything
*		heavy duty or rubust.
*
*		This returns a binary string of the reponse, or nil,rcode on
*		failure.
*
*	strerror(rcode)
*
*		Returns a string representation of the server response, or 
*		the return value from a failed query() call.  This function
*		does not fail (if it does, there's more to worry about).
*
*
* This code is written to C99.
*
***************************************************************************/

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>

#include <arpa/inet.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "dns.h"
#include "mappings.h"
#include "netsimple.h"

/********************************************************************/

static int dnslua_encode(lua_State *L)
{
  dns_question_t domain;
  dns_query_t    query;
  dns_align_t    buffer[DNS_BUFFER_UDP];
  size_t         len;
  int            qidx;
  int            rc;
  
  if (!lua_istable(L,1))
    luaL_typerror(L,1,lua_typename(L,LUA_TTABLE));
  
  memset(&domain,0,sizeof(domain));
  memset(&query, 0,sizeof(query));
  
  lua_getfield(L,1,"question");
  
  qidx = lua_gettop(L);
  
  if (!lua_istable(L,qidx))
    luaL_typerror(L,qidx,lua_typename(L,LUA_TTABLE));
  
  lua_getfield(L,qidx,"name");
  lua_getfield(L,qidx,"type");
  lua_getfield(L,qidx,"class");
  
  domain.name  = luaL_checkstring(L,-3);
  domain.type  = dns_type_value (luaL_optstring(L,-2,"A"));
  domain.class = dns_class_value(luaL_optstring(L,-1,"IN"));

  lua_getfield(L,1,"id");
  lua_getfield(L,1,"query");
  lua_getfield(L,1,"rd");
  lua_getfield(L,1,"opcode");
    
  query.id        = luaL_optint(L,-4,1234);
  query.query     = lua_toboolean(L,-3);
  query.rd        = lua_toboolean(L,-2);
  query.opcode    = dns_op_value(luaL_optstring(L,-1,"QUERY"));  
  query.qdcount   = 1;
  query.questions = &domain;
  len             = sizeof(buffer);
  rc              = dns_encode(buffer,&len,&query);
  
  if (rc != RCODE_OKAY)
  {
    lua_pushnil(L);
    lua_pushstring(L,dns_rcode_text(rc));
    return 2;
  }
  
  lua_pushlstring(L,(char *)buffer,len);
  return 1;
}

/********************************************************************/

static void push_dnsgpos_angle(lua_State *L,dnsgpos_angle *pa,bool lat)
{
  lua_createtable(L,0,4);
  lua_pushinteger(L,pa->deg);
  lua_setfield(L,-2,"deg");
  lua_pushinteger(L,pa->min);
  lua_setfield(L,-2,"min");
  lua_pushnumber(L,(double)pa->sec + ((double)pa->frac / 1000.0));
  lua_setfield(L,-2,"sec");
  lua_pushboolean(L,pa->nw);
  lua_setfield(L,-2,"nw");
  if (lat)
    lua_pushlstring(L,(pa->nw) ? "N" : "S" , 1);
  else
    lua_pushlstring(L,(pa->nw) ? "W" : "E" , 1);
  lua_setfield(L,-2,"hemisphere");
}

/********************************************************************/

static void decode_answer(
	lua_State    *L,
	int           tab,
	const char   *name,
	dns_answer_t *pans,
	size_t        cnt,
	bool          dup
)
{
  char ipaddr[INET6_ADDRSTRLEN];
  
  lua_createtable(L,cnt,0);
  
  for (size_t i = 0 ; i < cnt ; i++)
  {
    lua_pushinteger(L,i + 1);
    lua_createtable(L,0,0);
    
    lua_pushstring(L,pans[i].generic.name);
    lua_setfield(L,-2,"name");
    lua_pushinteger(L,pans[i].generic.ttl);
    lua_setfield(L,-2,"ttl");
    lua_pushstring(L,dns_class_text(pans[i].generic.class));
    lua_setfield(L,-2,"class");
    lua_pushstring(L,dns_type_text(pans[i].generic.type));
    lua_setfield(L,-2,"type");
    
    switch(pans[i].generic.type)
    {
      case RR_A:
           inet_ntop(AF_INET,&pans[i].a.address,ipaddr,sizeof(ipaddr));
           lua_pushstring(L,ipaddr);
           lua_setfield(L,-2,"address");
           lua_pushlstring(L,(char *)&pans[i].a.address,4);
           lua_setfield(L,-2,"raw_address");
           break;

      case RR_SOA:
           lua_pushstring(L,pans[i].soa.mname);
           lua_setfield(L,-2,"mname");
           lua_pushstring(L,pans[i].soa.rname);
           lua_setfield(L,-2,"rname");
           lua_pushnumber(L,pans[i].soa.serial);
           lua_setfield(L,-2,"serial");
           lua_pushnumber(L,pans[i].soa.refresh);
           lua_setfield(L,-2,"refresh");
           lua_pushnumber(L,pans[i].soa.retry);
           lua_setfield(L,-2,"retry");
           lua_pushnumber(L,pans[i].soa.expire);
           lua_setfield(L,-2,"expire");
           lua_pushnumber(L,pans[i].soa.minimum);
           lua_setfield(L,-2,"minimum");
           break;

      case RR_NAPTR:
           lua_pushinteger(L,pans[i].naptr.order);
           lua_setfield(L,-2,"order");
           lua_pushinteger(L,pans[i].naptr.preference);
           lua_setfield(L,-2,"preference");
           lua_pushstring(L,pans[i].naptr.flags);
           lua_setfield(L,-2,"flags");
           lua_pushstring(L,pans[i].naptr.services);
           lua_setfield(L,-2,"services");
           lua_pushstring(L,pans[i].naptr.regexp);
           lua_setfield(L,-2,"regexp");
           lua_pushstring(L,pans[i].naptr.replacement);
           lua_setfield(L,-2,"replacement");
           break;

      case RR_AAAA:
           inet_ntop(AF_INET6,&pans[i].aaaa.address,ipaddr,sizeof(ipaddr));
           lua_pushstring(L,ipaddr);
           lua_setfield(L,-2,"address");
           lua_pushlstring(L,(char *)&pans[i].aaaa.address,16);
           lua_setfield(L,-2,"raw_address");
           break;

      case RR_SRV:
           lua_pushinteger(L,pans[i].srv.priority);
           lua_setfield(L,-2,"priority");
           lua_pushinteger(L,pans[i].srv.weight);
           lua_setfield(L,-2,"weight");
           lua_pushinteger(L,pans[i].srv.port);
           lua_setfield(L,-2,"port");
           lua_pushstring(L,pans[i].srv.target);
           lua_setfield(L,-2,"target");
           break;
           
      case RR_WKS:
           inet_ntop(AF_INET,&pans[i].wks.address,ipaddr,sizeof(ipaddr));
           lua_pushstring(L,ipaddr);
           lua_setfield(L,-2,"address");
           lua_pushlstring(L,(char *)&pans[i].wks.address,4);
           lua_setfield(L,-2,"raw_address");
           lua_pushinteger(L,pans[i].wks.protocol);
           lua_setfield(L,-2,"protocol");
           lua_pushlstring(L,(char *)pans[i].wks.bits,pans[i].wks.numbits);
           lua_setfield(L,-2,"bits");
           break;
           
      case RR_GPOS:
           push_dnsgpos_angle(L,&pans[i].gpos.latitude,true);
           lua_setfield(L,-2,"longitude");
           push_dnsgpos_angle(L,&pans[i].gpos.longitude,false);
           lua_setfield(L,-2,"latitude");
           lua_pushnumber(L,pans[i].gpos.altitude);
           lua_setfield(L,-2,"altitude");
           break;
           
      case RR_LOC:
           lua_pushnumber(L,pans[i].loc.size);
           lua_setfield(L,-2,"size");
           lua_pushnumber(L,pans[i].loc.horiz_pre);
           lua_setfield(L,-2,"horiz_pre");
           lua_pushnumber(L,pans[i].loc.vert_pre);
           lua_setfield(L,-2,"vert_pre");
           push_dnsgpos_angle(L,&pans[i].loc.latitude,true);
           lua_setfield(L,-2,"latitude");
           push_dnsgpos_angle(L,&pans[i].loc.longitude,false);
           lua_setfield(L,-2,"longitude");
           lua_pushnumber(L,pans[i].loc.altitude);
           lua_setfield(L,-2,"altitude");
           break;

      case RR_NS:
           lua_pushstring(L,pans[i].ns.nsdname);
           lua_setfield(L,-2,"nsdname");
           break;
      case RR_CNAME:
           lua_pushstring(L,pans[i].cname.cname);
           lua_setfield(L,-2,"cname");
           break;
      case RR_MX:
           lua_pushstring(L,pans[i].mx.exchange);
           lua_setfield(L,-2,"exchange");
           lua_pushinteger(L,pans[i].mx.preference);
           lua_setfield(L,-2,"preference");
           break;
      case RR_PTR:
           lua_pushstring(L,pans[i].ptr.ptr);
           lua_setfield(L,-2,"ptr");
           break;
      case RR_HINFO:
           lua_pushstring(L,pans[i].hinfo.cpu);
           lua_setfield(L,-2,"cpu");
           lua_pushstring(L,pans[i].hinfo.os);
           lua_setfield(L,-2,"os");
           break;
      case RR_SPF:
      case RR_TXT:
           lua_pushlstring(L,pans[i].txt.text,pans[i].txt.len);
           lua_setfield(L,-2,"txt");
           break;
      default:
           break;
    }
    
    if (dup)
    {
      lua_getfield(L,-1,"name");
      lua_pushvalue(L,-2);
      lua_settable(L,-5);
    }
    
    lua_settable(L,-3);
  }
  
  lua_setfield(L,tab,name);         
}

/**********************************************************************/

static int dnslua_decode(lua_State *L)
{
  dns_align_t        bufresult[DNS_DECODEBUF_8K];
  dns_align_t        data     [DNS_BUFFER_UDP];
  const char        *luadata;
  dns_query_t       *result;
  size_t             size;
  int                tab;
  int                rc;
  
  /*---------------------------------------------------------------------
  ; We need to make sure our data is properly aligned.  And hey, this is
  ; Lua---a scripting lanague.  We can afford a bit of waste 8-)
  ;----------------------------------------------------------------------*/
  
  luadata = luaL_checklstring(L,1,&size);
  if (size > MAX_DNS_QUERY_SIZE) size = MAX_DNS_QUERY_SIZE;
  memcpy(data,luadata,size);
  
  rc = dns_decode(bufresult,sizeof(bufresult),data,size);
  
  if (rc != RCODE_OKAY)
  {
    lua_pushnil(L);
    lua_pushstring(L,dns_rcode_text(rc));
    return 2;
  }
  
  result = (dns_query_t *)bufresult;
  
  lua_createtable(L,0,0);
  tab = lua_gettop(L);
  
  lua_pushboolean(L,result->aa);
  lua_setfield(L,tab,"aa");
  lua_pushboolean(L,result->tc);
  lua_setfield(L,tab,"tc");
  lua_pushboolean(L,result->rd);
  lua_setfield(L,tab,"rd");
  lua_pushboolean(L,result->ra);
  lua_setfield(L,tab,"ra");
  lua_pushboolean(L,result->ad);
  lua_setfield(L,tab,"ad");
  lua_pushboolean(L,result->cd);
  lua_setfield(L,tab,"cd");
  lua_pushinteger(L,result->rcode);
  lua_setfield(L,tab,"rcode");
  
  if (result->qdcount)
  {
    lua_createtable(L,0,3);
    lua_pushstring(L,result->questions[0].name);
    lua_setfield(L,-2,"name");
    lua_pushstring(L,dns_class_text(result->questions[0].class));
    lua_setfield(L,-2,"class");
    lua_pushstring(L,dns_type_text(result->questions[0].type));
    lua_setfield(L,-2,"type");
    lua_setfield(L,tab,"question");
  }

  decode_answer(L,tab,"answers"     , result->answers    , result->ancount,false);
  decode_answer(L,tab,"nameservers" , result->nameservers, result->nscount,false);
  decode_answer(L,tab,"additional"  , result->additional , result->arcount,true);

  assert(tab == lua_gettop(L));
  
  return 1;
}

/*********************************************************************/

static int dnslua_strerror(lua_State *L)
{
  lua_pushstring(L,dns_rcode_text(luaL_checkint(L,1)));
  return 1;
}

/*********************************************************************/
  
static int dnslua_query(lua_State *L)
{
  sockaddr_all  remote;
  const char   *addr;
  const char   *luadata;
  size_t        size;
  dns_align_t   buffer[DNS_BUFFER_UDP];
  dns_align_t   data  [DNS_BUFFER_UDP];
  size_t        insize;
  int           rc;
  
  addr    = luaL_checkstring(L,1);  
  luadata = luaL_checklstring(L,2,&size);
  
  if (net_server(&remote,addr) < 0)
    luaL_error(L,"%s is not an IPv4/IPv6 address",addr);
  
  if (size > MAX_DNS_QUERY_SIZE) size = MAX_DNS_QUERY_SIZE;
  memcpy(data,luadata,size);
  insize = sizeof(buffer);
  rc = net_request(&remote,buffer,&insize,data,size);

  if (rc != 0)
  {
    lua_pushnil(L);
    lua_pushinteger(L,rc);
    return 2;
  }
  
  lua_pushlstring(L,(char *)buffer,insize);
  return 1;
}

/**********************************************************************/

static const struct luaL_reg reg_dns[] =
{
  { "encode"	, dnslua_encode		} ,
  { "decode"	, dnslua_decode		} ,
  { "strerror"	, dnslua_strerror	} ,
  { "query"	, dnslua_query		} ,
  { NULL	, NULL			} 
};

int luaopen_org_conman_dns(lua_State *L)
{
  luaL_register(L,"org.conman.dns",reg_dns);
  
  lua_pushliteral(L,"Copyright 2010 by Sean Conner.  All Rights Reserved.");
  lua_setfield(L,-2,"COPYRIGHT");
  
  lua_pushliteral(L,"Encode/Decode and send queries via DNS");
  lua_setfield(L,-2,"DESCRIPTION");
  
  lua_pushliteral(L,"0.0.1");
  lua_setfield(L,-2,"VERSION");
  
  return 1;
}

/**********************************************************************/

