/*

kHTTPd -- the next generation

Accept connections

*/
/****************************************************************
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2, or (at your option)
 *	any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ****************************************************************/

#include "structure.h"
#include "prototypes.h"
#include "sysctl.h"

#include <linux/smp_lock.h>

/*

Purpose:

AcceptConnections puts all "accepted" connections in the 
"WaitForHeader" queue.

Return value:
	The number of accepted connections
*/


int AcceptConnections(const int CPUNR, struct socket *Socket)
{
	struct http_request *NewRequest;
	struct socket *NewSock;
	int count = 0;
	int error;

	EnterFunction("AcceptConnections");
	
	if (atomic_read(&ConnectCount)>sysctl_khttpd_maxconnect)
	{
		LeaveFunction("AcceptConnections - to many active connections");
		return 0;
	}
	
	if (Socket==NULL) return 0;
	
	/* 
	   Quick test to see if there are connections on the queue.
	   This is cheaper than accept() itself because this saves us
	   the allocation of a new socket. (Which doesn't seem to be 
	   used anyway)
	*/
	lock_sock(Socket->sk);
   	if (Socket->sk->tp_pinfo.af_tcp.syn_wait_queue==NULL)
	{
	  	release_sock(Socket->sk);
		return 0;
	}
	release_sock(Socket->sk);
	
	error = 0;	
	while (error>=0)
	{
		NewSock = sock_alloc();
		if (NewSock==NULL)
			break;
			
		lock_kernel(); 	/* Required for the TCPIP-stack in 2.3.13 */
				/* This is actually bogus, since accept() releases
				   it immediatly again. This will be fixed
				   eventually though. */
			
			
		NewSock->type = Socket->type;
		(void)Socket->ops->dup(NewSock,Socket);
		
		
		error = Socket->ops->accept(Socket,NewSock,O_NONBLOCK);
		
		unlock_kernel();

		if ((error<0)&&(NewSock!=NULL))
		{
			sock_release(NewSock);
			break;
		}
		
			
		if (NewSock==NULL)
			break;
		
		
		
		/* Allocate a request-entry for the connection */
		NewRequest = kmalloc(sizeof(struct http_request),(int)GFP_KERNEL); 
		
		if (NewRequest == NULL)
		{
			Send50x(NewSock); 	/* Service not available. Try again later */
			sock_release(NewSock);
			error = -1;
			break;
		}
		memset(NewRequest,0,sizeof(struct http_request));  
		
		NewRequest->sock = NewSock;
		
		NewRequest->Next = threadinfo[CPUNR].WaitForHeaderQueue;
		
		init_waitqueue_entry(&NewRequest->sleep,current);
		
		add_wait_queue(NewSock->sk->sleep,&(NewRequest->sleep));
		
		threadinfo[CPUNR].WaitForHeaderQueue = NewRequest;
		
		atomic_inc(&ConnectCount);

	
		count++;
	}		
	
	LeaveFunction("AcceptConnections");
	return count;
}