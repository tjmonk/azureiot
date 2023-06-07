/*==============================================================================
MIT License

Copyright (c) 2023 Trevor Monk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
==============================================================================*/

/*!
 * @defgroup iothub iothub
 * @brief Connector for Azure IOTHub
 * @{
 */

/*============================================================================*/
/*!
@file iothub.c

    Azure IOTHub Connector

    The iothub Application instantiates an Azure IOTHub connection
    and creates a FIFO to allow clients to send data to the Azure
    IOT Hub via the iothub connector.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <varserver/varserver.h>
#include <openssl/ssl.h>
#include <azureiot/iothub_client.h>
#include <azureiot/iothubtransportamqp_websockets.h>
#include <azure_c_shared_utility/threadapi.h>
#include <azure_c_shared_utility/crt_abstractions.h>
#include <azureiot/iothubtransportamqp.h>
#include <azureiot/iothubtransporthttp.h>
#include <azureiot/iothubtransportamqp_websockets.h>
#include <uuid/uuid.h>


/*==============================================================================
        Private definitions
==============================================================================*/

/*! connection string name */
#define CONNECTION_STRING_NAME "/sys/iot/connection_string"

/*! connection string size */
#define CONNECTION_STRING_SIZE  ( 256 )

/*! message queue name */
#define MESSAGE_QUEUE_NAME "/iothub"

/*! maximum message size */
#define MAX_MESSAGE_SIZE ( 256 * 1024 )

/*! Array of message properties for the current message */
typedef struct msgProp
{
    /* pointer to the property name */
    const char *pKey;

    /* pointer to the property value */
    const char *pValue;

    /* pointer to the next property in the list */
    struct msgProp *pNext;
} MsgProp;

/*! IOTHub state */
typedef struct iothubState
{
    /*! variable server handle */
    VARSERVER_HANDLE hVarServer;

    /*! verbose flag */
    bool verbose;

    /*! pointer to the working copy of the message properties for the
        current message being processed */
    MsgProp *pMsgProperties;

    /*! pointer to the source of the current message */
    const char *pMsgSource;

    /*! message queue descriptor */
    mqd_t messageQueue;

    /*! pointer to the received message headers */
    unsigned char *rxHeaders;

    /*! pointer to the received message body */
    unsigned char *rxBody;

    /*! maximum length of a received message */
    size_t messageLength;

    /*! connection string */
    char connectionString[CONNECTION_STRING_SIZE];

    /*! IOT Hub Client Handle */
    IOTHUB_CLIENT_HANDLE iotHubClientHandle;

    /* count the number of message transmission attempts */
    uint32_t countTxTotal;

    /*! count the number of successful transmissions */
    uint32_t countTxOK;

    /*! count the number of transmission errors */
    uint32_t countTxErr;

} IOTHubState;

/*! The MsgContext structure returned as an argument
    to the async message callback status. */
typedef struct msgContext
{
    /*! handle to the message being transmitted */
    IOTHUB_MESSAGE_HANDLE messageHandle;

    /*! pointer to the IOTHubState object */
    IOTHubState *pState;

} MsgContext;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! iothub State object */
IOTHubState state;

/*==============================================================================
        Private function declarations
==============================================================================*/

void main(int argc, char **argv);
static int ProcessOptions( int argC, char *argV[], IOTHubState *pState );
static void usage( char *cmdname );
static void SetupTerminationHandler( void );
static void TerminationHandler( int signum, siginfo_t *info, void *ptr );
static int Connect( IOTHubState *pState );
static int LoadSettings( IOTHubState *pState );
static int ProcessMessage( IOTHubState *pState );
static int ProcessMessages( IOTHubState *pState);
static int GetBody( IOTHubState *pState,
                    uint32_t pid,
                    char **body,
                    size_t *len );

static int SendMessage( IOTHubState *pState,
                         char *headers,
                         char *body,
                         size_t len );

static void SendCallback( IOTHUB_CLIENT_CONFIRMATION_RESULT result,
                          void* userContextCallback);

static int BuildMessageProperties( MsgProp **ppProp, char *header );
static void ClearMessageProperties( MsgProp *pProp );
static int SetMessageProperties( IOTHUB_MESSAGE_HANDLE messageHandle,
                                 MsgProp *pMsgProp );
static int SetMessageProperty( IOTHUB_MESSAGE_HANDLE messageHandle,
                               MAP_HANDLE propMap,
                               const char *pKey,
                               const char *pValue );
static MsgProp **AddMessageProperty( MsgProp **ppProp,
                                     const char *pKey,
                                     const char *pValue );
static int SetupMessageQueue( IOTHubState *pState );
static void DestroyMessageQueue( IOTHubState *pState );

static IOTHUBMESSAGE_DISPOSITION_RESULT RxMsgHandler(
                                            IOTHUB_MESSAGE_HANDLE msg,
                                            void *userContext );

static mqd_t GetService( const char *service, size_t *len );

static char *SerializeMsg( IOTHUB_MESSAGE_HANDLE msg,
                           size_t maxlen,
                           size_t *totalLength );

static size_t AddProperty( char **p,
                           const char *key,
                           const char *value,
                           size_t *left );

/*==============================================================================
        Private function definitions
==============================================================================*/

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the iothub application

    The main function starts the iothub application

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @return none

==============================================================================*/
void main(int argc, char **argv)
{
    int result;

    /* clear the iothub state object */
    memset( &state, 0, sizeof( state ) );

    /* allocate memory for the message body */
    state.rxBody = calloc( 1, MAX_MESSAGE_SIZE );
    if ( state.rxBody != NULL )
    {
        /* set up an abnormal termination handler */
        SetupTerminationHandler();

        /* get a handle to the VAR server */
        state.hVarServer = VARSERVER_Open();

        /* load the IOTHUB settings */
        LoadSettings( &state );

        /* process the command line options */
        ProcessOptions( argc, argv, &state );

        /* connect to the IOT Hub */
        Connect( &state );

        /* set up the message queue */
        SetupMessageQueue( &state );

        /* Process received messages */
        ProcessMessages( &state );

        /* normally the service will not terminate,
           so we should not get here */
        if( state.hVarServer != NULL )
        {
            /* close the variable server */
            VARSERVER_Close( state.hVarServer );
        }

        /* destroy the message queue */
        DestroyMessageQueue( &state );
    }
}

/*============================================================================*/
/*  LoadSettings                                                              */
/*!
    Load the IOTHUB settings

    The LoadSettings function loads the IOTHUB settings
    from variable storage.

@param[in]
    pState
        pointer to the IOTHubState context

@retval EOK a connection to the IOTHUB was successfully established
@retval EINVAL invalid arguments
@retval other error as returned by VAR_GetStrByName

==============================================================================*/
static int LoadSettings( IOTHubState *pState )
{
    int result = EINVAL;

    if ( pState != NULL )
    {
        result = VAR_GetStrByName( pState->hVarServer,
                                   CONNECTION_STRING_NAME,
                                   pState->connectionString,
                                   CONNECTION_STRING_SIZE );
    }

    return result;
}

/*============================================================================*/
/*  Connect                                                                   */
/*!
    Connect to the IOTHUB

    The Connect function creates connection to the IOTHUB using the
    connection string specified in the IOTHUBState object.

@param[in]
    pState
        pointer to the IOTHubState which will contain the newly created message
        queue

@retval EOK a connection to the IOTHUB was successfully established
@retval EINVAL invalid arguments
@retval ENOENT connection to the IOTHUB failed
@retval EBADF no connection string specified
@retval ENOTSUP cannot set message callback handler

==============================================================================*/
static int Connect( IOTHubState *pState )
{
    int result = EINVAL;
    IOTHUB_CLIENT_TRANSPORT_PROVIDER transport;
    IOTHUB_CLIENT_HANDLE iotHubClientHandle;
    IOTHUB_CLIENT_RESULT icr;

    if ( pState != NULL )
    {
        if( pState->connectionString != NULL )
        {
            /* initialize the SSL library */
            SSL_library_init();

            /* select the transport protocol */
            transport = AMQP_Protocol_over_WebSocketsTls;

            /* create the connection */
            iotHubClientHandle = IoTHubClient_CreateFromConnectionString(
                                                    pState->connectionString,
                                                    transport);

            /* save the IOTHUB client handle */
            pState->iotHubClientHandle = iotHubClientHandle;

            if ( iotHubClientHandle != NULL )
            {
                IoTHubClient_SetOption( iotHubClientHandle,
                                        "logtrace",
                                        &(pState->verbose) );

                /* set up the receive message handler */
                icr = IoTHubClient_SetMessageCallback( iotHubClientHandle,
                                                       RxMsgHandler,
                                                       pState );
                if( icr == IOTHUB_CLIENT_OK )
                {
                    if( pState->verbose )
                    {
                        fprintf(stdout, "Connected\n");
                    }

                    result = EOK;
                }
                else
                {
                    /* cannot set message callback */
                    result = ENOTSUP;
                }
            }
            else
            {
                /* cannot create IOTHub client */
                result = ENOENT;
            }
        }
        else
        {
            /* no connection string */
            result = EBADF;
        }
    }

    return result;
}

/*============================================================================*/
/*  SetupMessageQueue                                                         */
/*!
    Set up the message queue

    The SetupMessageQueue function creates a new IOTHUB message queue
    which will receive messages from clients to send to an external IOTHUB

    This function will create a readonly message queue, and a receive buffer.

@param[in]
    pState
        pointer to the IOTHubState which will contain the newly created message
        queue

@retval EOK the message queue was successfully created
@retval EINVAL invalid arguments
@retval ENOMEM failed to create the receive message buffer
@retval other error as returned from mq_open

==============================================================================*/
static int SetupMessageQueue( IOTHubState *pState )
{
    int result = EINVAL;
    struct mq_attr attr;

    if ( pState != NULL )
    {
        /* initialize the message queue length */
        pState->messageLength = 0;

        /* create the IOTHub message queue */
        pState->messageQueue = mq_open( MESSAGE_QUEUE_NAME,
                                        O_RDONLY | O_CREAT,
                                        S_IRUSR | S_IWUSR,
                                        NULL );
        if ( pState->messageQueue != (mqd_t)-1 )
        {
            /* get the attributes */
            if ( mq_getattr(pState->messageQueue, &attr) != -1 )
            {
                pState->rxHeaders = calloc(1, attr.mq_msgsize + 1);
                if ( pState->rxHeaders != NULL )
                {
                    /* set the maximum size of received messages */
                    pState->messageLength = attr.mq_msgsize;
                    result = EOK;
                }
                else
                {
                    result = ENOMEM;
                }
            }
        }
        else
        {
            result = errno;
            fprintf(stderr, "%s\n", strerror(result));
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessMessages                                                           */
/*!
    Wait for and process IOTHUB messages

    The ProcessMessages function waits for messages received on the IOTHUB
    message queue and processes each of them as they arrive.

@param[in]
    pState
        pointer to the IOTHubState which contains the IOTHUB message queue
        to wait on.

@retval EINVAL invalid arguments
@retval other error as returned from ProcessMessage

==============================================================================*/
static int ProcessMessages( IOTHubState *pState)
{
    int result = EINVAL;

    if ( pState != NULL )
    {
        while( true )
        {
            /* wait for and process a message from the IOTHUB queue */
            result = ProcessMessage(pState);
            if( result != EOK )
            {
                fprintf( stderr,
                         "iothub: ProcessMessage: %s\n",
                         strerror( result ) );
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessMessage                                                            */
/*!
    Wait for and process an IOTHUB message

    The ProcessMessage function waits for a single message on the IOTHUB
    message queue and processes it when it arrives.

@param[in]
    pState
        pointer to the IOTHubState which contains the IOTHUB message queue
        to wait on.

@retval EOK a message was successfully received from the queue and processed
@retval EINVAL invalid arguments
@retval other error as returned from mq_receive

==============================================================================*/
static int ProcessMessage( IOTHubState *pState )
{
    int result = EINVAL;
    mqd_t mq;
    char *p;
    size_t len;
    unsigned int priority;
    ssize_t n;
    uint32_t pid;
    const char *preamble = "IOTC";
    char *headers;
    char *body;

    if ( pState != NULL )
    {
        mq = pState->messageQueue;
        p = pState->rxHeaders;
        len = pState->messageLength;

        /* wait for a message to arrive */
        n = mq_receive(mq, p, len, &priority);
        if ( n != -1 )
        {
            /* NUL terminate the message */
            p[n] = '\0';

            /* validate the preamble */
            if( memcmp( p, preamble, 4 ) == 0 )
            {
                /* get the client PID */
                memcpy( &pid, &p[4], 4 );

                /* get the headers */
                headers = &p[8];

                /* dump the message headers */
                if ( pState->verbose )
                {
                    fprintf(stdout, "headers:\n%s", headers);
                }

                /* get the message body */
                result = GetBody( pState, pid, &body, &len );
                if ( result == EOK )
                {
                    /* dump the message body */
                    if( pState->verbose )
                    {
                        fprintf(stdout, "body:\n%.*s\n", (int)len, body);
                    }

                    /* queue the message for delivery */
                    result = SendMessage( pState, headers, body, len );
                    if( result != EOK )
                    {
                        fprintf( stderr,
                                 "ProcessMessage: SendMessage: %s\n",
                                 strerror( result ) );
                    }
                }
                else
                {
                    fprintf( stderr,
                             "ProcessMessage: Cannot get body: %s\n",
                             strerror( result ) );
                }
            }
            else
            {
                fprintf(stderr, "ProcessMesssage: invalid preamble\n");
            }
        }
        else
        {
            fprintf(stderr, "ProcessMessage: %s", strerror(errno));
            result = errno;
        }
    }

    return result;
}

/*============================================================================*/
/*  GetBody                                                                   */
/*!
    Get the message body from the client application

    The GetBody function tries to read the message body from the client
    application which sent the message headers to the IOT message queue.
    It does this by reading the client's IOT FIFO.

    Body sizes up to 256KB are supported

@param[in]
    pState
        pointer to the IOTHubState object which provides the message
        reception buffer

@param[in]
    pid
        process id of the client application used to construct the FIFO name

@param[out]
    body
        pointer to a location to store the pointer to the received msg body

@param[out]
    len
        pointer to a location to store the received body length

@retval EOK a message body was successfully retrieved
@retval EINVAL invalid arguments
@retval ENOMEM no valid receive buffer is available
@retval other error as returned from mq_receive

==============================================================================*/
static int GetBody( IOTHubState *pState,
                    uint32_t pid,
                    char **body,
                    size_t *len )
{
    size_t totalBytes = 0;
    size_t bytesLeft;
    int fd;
    char fifoName[64];
    int n;
    char *rxBuf;
    int result = EINVAL;
    size_t chunkSize = BUFSIZ;
    char *p;

    if ( ( pState != NULL ) &&
         ( body != NULL ) &&
         ( len != NULL ) )
    {
        /* get a pointer to the receive buffer */
        rxBuf = pState->rxBody;
        if ( rxBuf != NULL )
        {
            /* construct the FIFO to read the message body from */
            sprintf(fifoName, "/tmp/iothub_%d", pid );

            /* open the FIFO */
            fd = open( fifoName, O_RDONLY );
            if ( fd != -1 )
            {
                /* intialize buffer position */
                p = rxBuf;
                bytesLeft = MAX_MESSAGE_SIZE;

                do
                {
                    /* try to read a chunk of data */
                    n = read(fd, p, (bytesLeft < BUFSIZ)
                                    ? bytesLeft
                                    : BUFSIZ);
                    if ( n >= 0 )
                    {
                        /* advance the buffer pointer */
                        p += n;

                        /* update the received bytes */
                        totalBytes += n;

                        /* update the bytes left */
                        bytesLeft -= n;
                    }
                    else
                    {
                        result = errno;
                    }
                } while ( ( n > 0 ) && ( totalBytes < MAX_MESSAGE_SIZE ));

                /* close the FIFO */
                close( fd );

                if ( ( n == 0 ) || ( totalBytes == MAX_MESSAGE_SIZE ) )
                {
                    /* read completed without error */
                    *body = rxBuf;
                    *len = totalBytes;
                    result = EOK;
                }
            }
            else
            {
                result = errno;
            }
        }
        else
        {
            result = ENOMEM;
        }
    }

    return result;
}

/*============================================================================*/
/*  BuildMessageProperties                                                    */
/*!
    Build the list of message properties from the specified message header

    The BuildMessageProperties function extracts the message properties
    from the specified message header.  The properties are expected to
    be one per line, with the property name and value separated by
    a colon.  Each line must be separated by a linefeed '\n', and
    the final property must be terminated with two linefeeds: "\n\n"

    eg

    property-1:value-1\n
    property-2:value-2\n
    ...
    property-last:value-last\n
    \n

    The function stores extracted properties into the specified property
    list, automatically extending the list if its length is exceeded.
    There is currently no enforced maximum property list length.

    @param[in]
        ppProp
            pointer to a pointer to the message property list.

    @param[in]
        header
            pointer to a NUL terminated string containing the property list
            as described above

    @retval EOK the property list was populated successfully
    @retval ENOMEM unable to allocate memory for the property list
    @retval EINVAL invalid arguments

==============================================================================*/
static int BuildMessageProperties( MsgProp **ppProp, char *header )
{
    int result = EINVAL;
    const char *pKey;
    const char *pValue;
    char *p;
    const char *pStart;
    bool done = false;
    int propertyCount = 0;
    int state = 0;
    char c;
    int rc;

    if ( ( ppProp != NULL ) &&
         ( header != NULL ) )
    {
        /* clear out any previous message properties */
        ClearMessageProperties( *ppProp );

        /* initial conditions */
        p = header;
        pStart = header;

        while( !done && ( ppProp != NULL ) )
        {
            c = *p;

            switch( state )
            {
                case 0:
                default:
                    /* looking for key */
                    if ( c == ':' )
                    {
                        *p = '\0';
                        pKey = pStart;
                        pStart = p+1;
                        state = 1;
                    }
                    else if ( ( c == '\n' ) || ( c == 0 ) )
                    {
                        done = true;
                    }
                    break;

                case 1:
                    /* looking for value */
                    if ( ( c == '\n' ) || ( c == 0 ) )
                    {
                        pValue = pStart;

                        if ( c == 0 )
                        {
                            done = true;
                        }
                        else
                        {
                            *p = '\0';
                            pStart = p+1;
                            state = 0;
                        }

                        /* add message property */
                        ppProp = AddMessageProperty( ppProp, pKey, pValue );
                    }
                    break;
            }

            p++;
        }

        /* check for memory allocation failure */
        result = (ppProp != NULL ) ? EOK : ENOMEM;
    }

    return result;
}

/*============================================================================*/
/*  AddMessageProperty                                                        */
/*!
    Add a message property to the list of IOTHub message properties

    The AddMessageProperty function adds a new message property
    specified by the property key and value to the specified
    MsgProp list.  The MsgProp argument points to the pointer to
    the next MsgProp object to be populated.  If the "next" MsgProp
    pointer is NULL, then a new MsgProp object is allocated on the
    heap, populated, and appended at the location specifed by
    the ppProp argument.

    @param[in]
        ppProp
            pointer to a pointer to the next MsgProp object to
            be populated.  If the next MsgProp object is NULL,
            it will be allocated.

    @param[in]
        pKey
            pointer to a NUL terminated string containing the
            property name

    @param[in]
        pValue
            pointer to a NUL terminated string containing the
            property value

    @retval pointer to the pointer to the next MsgProp object to be populated
    @retval NULL invalid arguments or memory allocation failure

==============================================================================*/
static MsgProp **AddMessageProperty( MsgProp **ppProp,
                                     const char *pKey,
                                     const char *pValue )
{
    MsgProp **result = NULL;

    if ( ( ppProp != NULL ) &&
         ( pKey != NULL ) &&
         ( pValue != NULL ) )
    {
        if( *ppProp == NULL )
        {
            /* allocate memory for a new message property */
            *ppProp = malloc( sizeof( MsgProp ) );
            if ( *ppProp != NULL )
            {
                /* create a new MsgProp object */
                (*ppProp)->pKey = pKey;
                (*ppProp)->pValue = pValue;
                (*ppProp)->pNext = NULL;

                /* return a pointer to the pNext field of the new property */
                result = &((*ppProp)->pNext);
            }
        }
        else
        {
            /* fill an existing MsgProp object */
            (*ppProp)->pKey = pKey;
            (*ppProp)->pValue = pValue;

            /* return a pointer to the pNext field of the new property */
            result = &((*ppProp)->pNext);
        }
    }

    return result;
}

/*============================================================================*/
/*  ClearMessageProperties                                                    */
/*!
    Clear the specified message properties list

    The ClearMessageProperties function clears the specified message property
    list by iterating through the list and setting the property key and
    value pointers to NULL.

    Note, it is not the responsibilty of this function to deallocate any
    memory on the heap used for these keys and values.

    This function returns no results

    @param[in]
        pProp
            pointer to the MsgProp list to clear
            a pointer to the next MsgProp object to

==============================================================================*/
static void ClearMessageProperties( MsgProp *pProp )
{
    while ( pProp != NULL )
    {
        pProp->pKey = NULL;
        pProp->pValue = NULL;

        pProp = pProp->pNext;
    }
}

/*============================================================================*/
/*  SetMessageProperties                                                      */
/*!
    Set the properties in the IOTHub Message

    The SetMessageProperties function copies the message properties from
    the specified MsgProp list into the IOTHUB_MESSAGE.

    @param[in]
        messageHandle
            handle to the IOTHUB_MESSAGE to populate

    @param[in]
        pMsgProp
            pointer to the MsgProp list to assign to the message

    @retval EOK message properties were assigned successfully
    @retval EINVAL invalid arguments

==============================================================================*/
static int SetMessageProperties( IOTHUB_MESSAGE_HANDLE messageHandle,
                                 MsgProp *pMsgProp )
{
    int result = EINVAL;
    MAP_HANDLE propMap;
    int rc;

    if ( ( pMsgProp != NULL ) &&
         ( messageHandle != NULL ) )
    {
        result = EOK;

        /* get the property map for the message */
        propMap = IoTHubMessage_Properties(messageHandle);
        if( propMap != NULL )
        {
            while ( pMsgProp != NULL )
            {
                if( pMsgProp->pKey != NULL )
                {
                    /* set the message property */
                    rc = SetMessageProperty( messageHandle,
                                                propMap,
                                                pMsgProp->pKey,
                                                pMsgProp->pValue );
                    if ( rc != EOK )
                    {
                        result = rc;
                    }
                }
                else
                {
                    /* no more properties */
                    break;
                }

                pMsgProp = pMsgProp->pNext;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  SetMessageProperty                                                        */
/*!
    Set a property in the IOTHub Message

    The SetMessageProperty function sets a single message property in the
    IOTHUB_MESSAGE.

    There are two special cases:

    1) If the message property is 'messageId' then the message identifier
    is set via the IoTHubMessage_SetMessageId function.

    2) If the message property is 'correlationId' then the correlation
    identifier is set via the IoTHubMessage_SetCorrelationId function.

    All other properties are added as user-properties via the Map_AddOrUpdate
    function.

    @param[in]
        messageHandle
            handle to the IOTHUB_MESSAGE to populate

    @param[in]
        propMap
            handle to the property map for the message

    @param[in]
        pKey
            pointer to a NUL terminated string containing the property name

    @param[in]
        pValue
            pointer to a NUL terminated string containing the property value

    @retval EOK the message property was assigned successfully
    @retval EINVAL invalid arguments
    @retval ENOTSUP failed to set the message or correlation identifier
    @retval ENOENT failed to add the custom user property

==============================================================================*/
static int SetMessageProperty( IOTHUB_MESSAGE_HANDLE messageHandle,
                               MAP_HANDLE propMap,
                               const char *pKey,
                               const char *pValue )
{
    int result = EINVAL;
    IOTHUB_MESSAGE_RESULT imr = IOTHUB_MESSAGE_INVALID_ARG;
    MAP_RESULT mr = MAP_INVALIDARG;

    if ( ( messageHandle != NULL ) &&
         ( propMap != NULL ) &&
         ( pKey != NULL ) &&
         ( pValue != NULL ) )
    {
        result = EOK;

        if( strncmp( pKey, "correlationId", 13 ) == 0 )
        {
            /* set the message correlation identifier from the
               supplied property */
            imr = IoTHubMessage_SetCorrelationId( messageHandle, pValue );
            if ( imr != IOTHUB_MESSAGE_OK )
            {
                result = ENOTSUP;
            }
        }
        else if( strncmp( pKey, "messageId", 9) == 0 )
        {
            /* set the message identifier from the supplied property */
            imr = IoTHubMessage_SetMessageId( messageHandle, pValue );
            if ( imr != IOTHUB_MESSAGE_OK )
            {
                result = ENOTSUP;
            }
        }
        else
        {
            /* set custom message properties */
            mr = Map_AddOrUpdate( propMap, pKey, pValue );
            if ( mr != MAP_OK )
            {
                result = ENOENT;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  SendMessage                                                               */
/*!
    Send an IOTHub Message

    The SendMessage function sends the requested message to the IOTHUB service

    It creates the mssage from the specified body.
    If the (optional) headers are specified they are parsed and added to
    the message.

    The message is the queued for delivery.

    @param[in]
        pState
            pointer to the IOTHubState

    @param[in]
        headers
            pointer to the (optional) headers specified one per line as
            key:value pairs separated by a colon.  The last header is
            terminated with two newlines.

    @param[in]
        body
            pointer to the byte array to send.  This byte array may
            contain ASCII or binary data and does not need to be NUL
            terminated

    @param[in]
        len
            length of the byte array to send.

    @retval EOK the message was queued for delivery
    @retval ENOMEM could not allocate memory for the message headers
    @retval EIO message could not be queued for delivery
    @retval EBADMSG could not create IOTHUB message from the byte array
    @retval EBADF no connection to the IOTHUB

==============================================================================*/
static int SendMessage( IOTHubState *pState,
                        char *headers,
                        char *body,
                        size_t len )
{
    IOTHUB_CLIENT_HANDLE iotHubClientHandle;
    IOTHUB_CLIENT_RESULT icr;
    IOTHUB_MESSAGE_HANDLE messageHandle;
    int result = EINVAL;
    MsgContext *pMsgContext;
    const char *pMsgId;
    uuid_t uuid;
    char messageId[64];

    char *p;

    if( ( pState != NULL ) &&
        ( body != NULL ) &&
        ( len > 0 ) )
    {
        /* create the connection */
        iotHubClientHandle = pState->iotHubClientHandle;
        if ( iotHubClientHandle != NULL )
        {
            /* build the message content from the body of the message */
            messageHandle = IoTHubMessage_CreateFromByteArray( body, len );
            if( messageHandle != NULL )
            {
                if ( headers != NULL )
                {
                    p = strdup( headers );
                    if ( p != NULL )
                    {
                        BuildMessageProperties( &pState->pMsgProperties, p );
                        SetMessageProperties( messageHandle,
                                              pState->pMsgProperties );
                        free( p );
                    }
                    else
                    {
                        result = ENOMEM;
                    }
                }

                /* get the message id */
                pMsgId = IoTHubMessage_GetMessageId( messageHandle );
                if( pMsgId == NULL )
                {
                    uuid_generate( uuid );
                    uuid_unparse( uuid, messageId );
                    IoTHubMessage_SetMessageId( messageHandle, messageId );
                }

                if( pState->verbose )
                {
                    pMsgId = IoTHubMessage_GetMessageId( messageHandle );
                    if ( pMsgId != NULL )
                    {
                        fprintf( stdout,
                                 "\x1b[33mSending message: %s\x1b[0m\n",
                                 pMsgId );
                    }
                    else
                    {
                        fprintf( stderr,
                                 "\x1b[31mNo message id\x1b[0m\n");
                    }
                }

                /* create a message context object */
                pMsgContext = malloc( sizeof( MsgContext ) );
                if( pMsgContext != NULL )
                {
                    pMsgContext->pState = pState;
                    pMsgContext->messageHandle = messageHandle;
                }

                /* send the message back */
                icr = IoTHubClient_SendEventAsync( iotHubClientHandle,
                                                   messageHandle,
                                                   SendCallback,
                                                   pMsgContext );
                if ( icr == IOTHUB_CLIENT_OK)
                {
                    result = EOK;
                }
                else
                {
                    result = EIO;
                }
            }
            else
            {
                result = EBADMSG;
            }
        }
        else
        {
            result = EBADF;
        }
    }

    return result;
}

/*============================================================================*/
/*  SendCallback                                                              */
/*!
    Async send callback function

    The SendCallback function is invoked from the IOT SDK framework
    when a message transmission has completed (successfully or
    unsuccessfully)

    @param[in]
       result
            status of the message transfer attempt

    @retval[in]
        userContextCallback

    @return none

==============================================================================*/
static void SendCallback( IOTHUB_CLIENT_CONFIRMATION_RESULT result,
                          void* userContextCallback)
{
    IOTHubState *pState;
    MsgContext *pContext = (MsgContext *)userContextCallback;
    IOTHUB_MESSAGE_HANDLE messageHandle;
    const char *pMessageId;
    const char *red = "\x1b[31m";
    const char *green = "\x1b[32m";
    const char *color;

    if ( pContext != NULL )
    {
        pState = pContext->pState;
        messageHandle = pContext->messageHandle;

        if( ( pState != NULL ) &&
            ( messageHandle != NULL ) )
        {
            /* get the message id */
            pMessageId = IoTHubMessage_GetMessageId( messageHandle );
            if( pMessageId == NULL )
            {
                pMessageId = "unknown";
            }

            /* set the notification color */
            color = (result == IOTHUB_CLIENT_OK) ? green : red;

            if( pState->verbose )
            {
                fprintf( stdout,
                         "%s%s: Message Send %s\x1b[0m\n",
                         color,
                         pMessageId,
                         MU_ENUM_TO_STRING( IOTHUB_CLIENT_CONFIRMATION_RESULT,
                                            result ) );
            }

            switch( result )
            {
                case IOTHUB_CLIENT_OK:
                    pState->countTxOK++;
                    break;

                default:
                    pState->countTxErr++;
                    break;
            }
        }

        /* free the memory used for the message context */
        free( pContext );
    }
}

/*============================================================================*/
/*  usage                                                                     */
/*!
    Display the application usage

    The usage function dumps the application usage message
    to stderr.

    @param[in]
       cmdname
            pointer to the invoked command name

    @return none

============================================================================*/
static void usage( char *cmdname )
{
    if( cmdname != NULL )
    {
        fprintf(stderr,
                "usage: %s [-v] [-h]\n"
                " [-h] : display this help\n"
                " [-c connection string] : set the IOTHub connection string\n"
                " [-v] : verbose output\n",
                cmdname );
    }
}

/*============================================================================*/
/*  ProcessOptions                                                            */
/*!
    Process the command line options

    The ProcessOptions function processes the command line options and
    populates the iothubtate object

    @param[in]
        argC
            number of arguments
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @param[in]
        pState
            pointer to the iothub state object

    @return none

==============================================================================*/
static int ProcessOptions( int argC, char *argV[], IOTHubState *pState )
{
    int c;
    int result = EINVAL;
    const char *options = "hvc:";

    if( ( pState != NULL ) &&
        ( argV != NULL ) )
    {
        while( ( c = getopt( argC, argV, options ) ) != -1 )
        {
            switch( c )
            {
                case 'v':
                    pState->verbose = true;
                    break;

                case 'h':
                    usage( argV[0] );
                    break;

                case 'c':
                    /* get the connection string */
                    if ( strlen(optarg) < CONNECTION_STRING_SIZE )
                    {
                        strcpy( pState->connectionString, optarg );
                    }
                    else
                    {
                        syslog( LOG_ERR, "invalid connectionstring\n" );
                    }
                    break;

                default:
                    break;

            }
        }
    }

    return 0;
}

/*============================================================================*/
/*  SetupTerminationHandler                                                   */
/*!
    Set up an abnormal termination handler

    The SetupTerminationHandler function registers a termination handler
    function with the kernel in case of an abnormal termination of this
    process.

==============================================================================*/
static void SetupTerminationHandler( void )
{
    static struct sigaction sigact;

    memset( &sigact, 0, sizeof(sigact) );

    sigact.sa_sigaction = TerminationHandler;
    sigact.sa_flags = SA_SIGINFO;

    sigaction( SIGTERM, &sigact, NULL );
    sigaction( SIGINT, &sigact, NULL );

}

/*============================================================================*/
/*  DestroyMessageQueue                                                       */
/*!
    Destroy the message queue

    The DestroyMessageQueue function closes and deletes the IOTHUB
    message queue.

@param[in]
    pState
        pointer to the IOTHubState containing the message queue to destroy

==============================================================================*/
static void DestroyMessageQueue( IOTHubState *pState )
{
    if ( pState != NULL )
    {
        if( pState->messageQueue != (mqd_t)-1)
        {
            mq_close( pState->messageQueue );
            pState->messageQueue = -1;
        }

        if( pState->rxHeaders != NULL )
        {
            free( pState->rxHeaders);
            pState->rxHeaders = NULL;
        }

        /* remove the message queue from the system */
        mq_unlink( MESSAGE_QUEUE_NAME );
    }
}

/*============================================================================*/
/*  TerminationHandler                                                        */
/*!
    Abnormal termination handler

    The TerminationHandler function will be invoked in case of an abnormal
    termination of this process.  The termination handler closes
    the connection with the variable server and cleans up its VARFP shared
    memory.

@param[in]
    signum
        The signal which caused the abnormal termination (unused)

@param[in]
    info
        pointer to a siginfo_t object (unused)

@param[in]
    ptr
        signal context information (ucontext_t) (unused)

==============================================================================*/
static void TerminationHandler( int signum, siginfo_t *info, void *ptr )
{
    syslog( LOG_ERR, "Abnormal termination of iothub\n" );
    VARSERVER_Close( state.hVarServer );

    /* destroy the message queue */
    DestroyMessageQueue( &state );

    exit( 1 );
}

/*============================================================================*/
/*  RxMsgHandler                                                              */
/*!
    Received Message Handler

    The RxMsgHandler function is invoked by the IOT SDK Framework to
    handle received cloud-to-device messages.  It is installed using the
    IoTHubClient_SetMessageCallback function.

    It looks for a "service" property in the received message to determine
    which message handler will process the received message.

    It then serializes the received message into a message buffer,
    and sends it to the message handler for further processing.

@param[in]
    msg
        Handle to the received message

@param[in]
    userContext
        pointer to the IOTHubState which was provided when the
        RxMsgHandler function was installed.

@retval IOTHUBMESSAGE_REJECTED if the message can not be processed
@retval IOTHUBMESSAGE_ACCEPTED if the message was accepted for processing

==============================================================================*/
static IOTHUBMESSAGE_DISPOSITION_RESULT RxMsgHandler( IOTHUB_MESSAGE_HANDLE msg,
                                                      void *userContext )
{
    IOTHUBMESSAGE_DISPOSITION_RESULT result = IOTHUBMESSAGE_REJECTED;
    IOTHubState *pState = (IOTHubState *)userContext;
    MAP_HANDLE propMap;
    const char *service;
    char *pMsg;
    size_t maxlen;
    size_t totalLength;
    mqd_t mq;

    const char*const* keys = NULL;
    const char*const* values = NULL;
    size_t propCount = 0;
    MAP_RESULT mr;
    int i;

    if( ( pState != NULL ) &&
        ( msg != NULL ) )
    {
        /* get the message properties */
        propMap = IoTHubMessage_Properties( msg );
        if ( propMap != NULL )
        {
            mr = Map_GetInternals(propMap, &keys, &values, &propCount);
            if ( mr == MAP_OK )
            {
                for( i = 0; i < propCount; i++ )
                {
                    printf("%s:%s\n", keys[i], values[i]);
                }
            }

            /* get the name of the service we need to connect to */
            service = Map_GetValueFromKey( propMap, "service");

            /* connect to the service */
            mq = GetService( service, &maxlen );
            if ( mq != -1 )
            {
                /* serialize the message to send to the service */
                pMsg = SerializeMsg( msg, maxlen, &totalLength );
                if( pMsg != NULL )
                {
                    printf("Sending %ld bytes\n", totalLength);
                    /* send the message to the service */
                    if( mq_send( mq, pMsg, totalLength, 0) == 0 )
                    {
                        result = IOTHUBMESSAGE_ACCEPTED;
                    }
                    else
                    {
                        printf("Cannot send message\n");
                    }

                    /* deallocate memory used by the message */
                    free( pMsg );
                }
                else
                {
                    printf("Cannot serialize message\n");
                }

                /* close the connection to the service */
                mq_close( mq );
            }
            else
            {
                printf("Cannot get service: %s\n", service);
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  GetService                                                                */
/*!
    Get handler for a received message

    The GetService function gets a handle to the service which will
    process the received message.

    @param[in]
        service
            name of the service to handle the received message

    @param[in]
        len
            pointer to a location to store the maximum message size
            the service will allow.

    @retval handle to the message processing service
    @retval -1 if the requested message processing service does not exist

==============================================================================*/
static mqd_t GetService( const char *service, size_t *len )
{
    char *servicepath = NULL;
    mqd_t mq = -1;
    struct mq_attr attr;

    if( ( service != NULL ) &&
        ( len != NULL ) )
    {
        /* generate the service path name */
        if( asprintf(&servicepath, "/%s", service ) > 0 )
        {
            /* try to open the service */
            mq = mq_open( servicepath, O_WRONLY );
            if ( mq != -1 )
            {
                /* get the maximum message size allowed by the service */
                if( mq_getattr(mq, &attr ) != -1 )
                {
                    /* store the maximum message size the service allows */
                    *len = attr.mq_msgsize;
                }
                else
                {
                    /* close the connection to the service */
                    mq_close( mq );
                    mq = -1;
                }
            }
        }
    }

    /* return the handle to the message processing service */
    return mq;
}

/*============================================================================*/
/*  SerializeMsg                                                              */
/*!
    Serialize IOTHUB message into a message buffer

    The BuildRxMsg function serializes a received message into a message
    buffer.  It inserts the special message properties "messageId" and
    "correlationId".  It inserts all the user properties.
    Each property is inserted into the message buffer as a "key:value\n"
    pair.  It then inserts an additional newline "\n" character to
    separate the message header and message body, and then inserts the
    message body byte array.

    If the constructed message exceeds the available space in the buffer
    then the message construction fails and no message is generated.

    @param[in]
        msg
            handle to the IOTHUB message to serialize

    @param[in]
        propMap
            handle to the message's properties

    @param[in]
        maxlen
            maximium length of the serialized message

    @param[out]
        totalLength
            pointer to a location containing the total length of the
            serialized message.

    @retval pointer to the serialized message
    @retval NULL if the message could not be serialized due to lack of space

==============================================================================*/
static char *SerializeMsg( IOTHUB_MESSAGE_HANDLE msg,
                           size_t maxlen,
                           size_t *totalLength )

{
    char *pMsg = NULL;
    char *p;
    const char *messageId;
    const char *correlationId;
    const char*const* keys = NULL;
    const char*const* values = NULL;
    const unsigned char *body;
    size_t left = maxlen;
    size_t bodySize;
    size_t propCount = 0;
    size_t len = 0;
    MAP_RESULT mr;
    MAP_HANDLE propMap;
    IOTHUB_MESSAGE_RESULT imr;
    IOTHUBMESSAGE_CONTENT_TYPE ct;

    int i;

    if ( ( msg != NULL ) &&
         ( totalLength != NULL ) )
    {
        printf("Allocating %ld bytes\n",maxlen);
        pMsg = calloc( 1, maxlen );
        if( pMsg != NULL )
        {
            p = pMsg;

            /* store the message ID */
            messageId = IoTHubMessage_GetMessageId( msg );
            printf("messageId: %s\n", messageId);
            len += AddProperty( &p,
                                "messageId",
                                messageId,
                                &left );

            printf("len=%ld\n", len);

            /* store the correlation ID */
            correlationId = IoTHubMessage_GetCorrelationId( msg );
            printf("correlationId:%s\n", correlationId);
            len += AddProperty( &p,
                                "correlationId",
                                correlationId,
                                &left );

            printf("len=%ld\n", len);

            /* store the message properties */
            propMap = IoTHubMessage_Properties( msg );
            if ( propMap != NULL )
            {
                mr = Map_GetInternals(propMap, &keys, &values, &propCount);
                if ( mr == MAP_OK )
                {
                    for( i = 0; i < propCount; i++ )
                    {
                        printf("Adding property: %s\n", keys[i]);
                        len += AddProperty( &p, keys[i], values[i], &left );
                        printf("len=%ld\n", len);
                    }
                }
            }

            /* store the message body */
            printf("Getting Message Body\n");
            ct = IoTHubMessage_GetContentType( msg );
            if ( ct == IOTHUBMESSAGE_BYTEARRAY )
            {
                imr = IoTHubMessage_GetByteArray( msg, &body, &bodySize );
                printf("Message type is byte array\n");
            }
            else if ( ct == IOTHUBMESSAGE_STRING )
            {
                printf("Message type is string\n");
                body = IoTHubMessage_GetString( msg );
                bodySize = strlen( body );
            } else {
                /* no message body */
                body = "{}";
                bodySize = strlen(body);
            }

            printf("bodySize = %ld\n", bodySize);
            printf("left = %ld\n", left);
            printf("body: %.*s\n", (int)bodySize, body);

            /* check if we have enough room for the message
               body, a newline, and a NUL terminator */
            if( left > bodySize + 1 )
            {
                /* insert header/body delimeter */
                *p++ = '\n';
                len++;
                printf("len=%ld\n", len);
                left--;

                /* copy the body */
                memcpy( p, body, bodySize );
                len += bodySize;
                left -= bodySize;

                /* add NUL terminator */
                p[bodySize] = 0;
                len++;

                printf("pMsg = %s\n", pMsg);
                *totalLength = len;
                printf("*totalLength = %ld\n", *totalLength );
            }
            else
            {
                /* not enough space for the message body */
                printf("Not enough space for message body\n");
                free( pMsg );
                *totalLength = 0;
                printf("totalLength=%ld\n", *totalLength);

                pMsg = NULL;
            }
        }
    }

    return pMsg;
}

/*============================================================================*/
/*  AddProperty                                                               */
/*!
    Add a key/value property to a message buffer

    The AddProperty function adds a key/value message property to a message
    buffer.  It adds the property using the following format:

    key:value\n

    It calculates the length of the property string and ensures there is
    enough space left in the message buffer and then updates the insertion
    point of the buffer, and the number of bytes remaining in the buffer.

    @param[in,out]
        p
            pointer to a pointer to the insertion point in the buffer.

    @param[in]
        key
            property name string

    @param[in]
        value
            property value string

    @param[out]
        left
            pointer to a location containing the number of bytes remaining
            in the buffer

    @returns the number of bytes added to the buffer

==============================================================================*/
static size_t AddProperty( char **p,
                           const char *key,
                           const char *value,
                           size_t *left )
{
    size_t len = 0;

    if ( ( p != NULL ) &&
         ( *p != NULL ) &&
         ( key != NULL ) &&
         ( value != NULL ) &&
         ( left != NULL ))
    {
        len = strlen( key ) + strlen ( value ) + 2;
        if( *left > len )
        {
            if( snprintf(*p, *left, "%s:%s\n", key, value ) == len )
            {
                *left -= len;
                *p += len;
            }
        }
        else
        {
            len = 0;
        }
    }

    return len;
}

/*! @}
 * end of iothub group */
