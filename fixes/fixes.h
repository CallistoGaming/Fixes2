/*  
 *  Version: MPL 1.1
 *  
 *  The contents of this file are subject to the Mozilla Public License Version 
 *  1.1 (the "License"); you may not use this file except in compliance with 
 *  the License. You may obtain a copy of the License at 
 *  http://www.mozilla.org/MPL/
 *  
 *  Software distributed under the License is distributed on an "AS IS" basis,
 *  WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 *  for the specific language governing rights and limitations under the
 *  License.
 *  
 *  The Original Code is the sscanf 2.0 SA:MP plugin.
 *  
 *  The Initial Developer of the Original Code is Alex "Y_Less" Cole.
 *  Portions created by the Initial Developer are Copyright (C) 2010
 *  the Initial Developer. All Rights Reserved.
 *  
 *  Contributor(s):
 *  
 *  Special Thanks to:
 *  
 *  SA:MP Team past, present and future
 */

#pragma once

typedef
	void (* logprintf_t)(char *, ...);

typedef
	char * (* GetServer_t)();

#define CHECK_PARAMS_EQ(m,n) do{if(m[0] != n * 4){logprintf("fixes.plugin: Incorrect parameters passed, %d != %d.", m[0] / 4, n);return 0;}}while(0)
#define CHECK_PARAMS_GTE(m,n) do{if(m[0] < n * 4){logprintf("fixes.plugin: Incorrect parameters passed, %d < %d.", m[0] / 4, n);return 0;}}while(0)
