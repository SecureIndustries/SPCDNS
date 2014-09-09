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
* Definitions for DNS record output functions used by sample application.
*
* It was factored out so it could be called from client applications.
*
***************************************************************************/

#ifndef OUTPUT_H
#define OUTPUT_H

#ifdef __cplusplus
  extern "C" {
#endif

#ifndef __GNUC__
#  define __attribute__(x)
#endif

#include "dns.h"

void dns_print_result	(dns_query_t *);
void dns_print_header	(dns_query_t *);
void dns_print_question	(const char *,dns_question_t *,size_t);
void dns_print_answer	(const char *,dns_answer_t   *,size_t);
void dns_dump_memory	(FILE *,const void *,size_t,size_t);

#ifdef __cplusplus
  }
#endif
#endif
