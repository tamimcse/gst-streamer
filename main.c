#include <gst/gst.h>
#include <glib.h>
#include <math.h>
#include <string.h>
#include <gio/gio.h>
#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/socket.h>  
#include <stdint.h>  
#include <linux/tcp.h>
//#include <netinet/tcp.h>

typedef struct _CustomData {
  gboolean is_live;  
  GstElement *pipeline;
  GMainLoop *loop;
  GstElement *tcp_svr_sink;
  GstElement *encoder;
  GstElement *decodebin;
  GstElement *source;
  GstElement *video_converter;
  GSocket *socket;
} CustomData;

static void
cb_new_pad (GstElement *element,
        GstPad     *pad,
        CustomData *data)
{
  g_print ("A new pad was created\n");    
  if(!gst_element_link_many (data->decodebin, data->encoder, data->tcp_svr_sink, NULL))
  {
    g_printerr ("Cannot link elements.\n");          
  }
}

static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;

    g_print ("Got %s message\n", GST_MESSAGE_TYPE_NAME (msg));
  
  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;
      
    case GST_MESSAGE_BUFFERING:
        g_print("Buffering...");
        break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static void
handle_sync_message (GstBus * bus, GstMessage * message, CustomData *data)
{
  GMainLoop *loop = data->loop;
  
  g_print ("Got %s message\n", GST_MESSAGE_TYPE_NAME (message));

  switch (message->type) {
    case GST_MESSAGE_STEP_DONE:
      break;
    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;
      
    case GST_MESSAGE_BUFFERING:{
      gint percent = 0;

      /* If the stream is live, we do not care about buffering. */
      if (data->is_live) break;

      gst_message_parse_buffering (message, &percent);
      g_print ("Buffering (%3d%%)\r", percent);
      /* Wait until buffering is complete before start/resume playing */
      if (percent < 100)
        gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
      else
        gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
      break;
    }

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (message, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_CLOCK_LOST:
        /* Get a new clock */
        gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
        gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
        break;    
    default:
      break;
  }
}

void sock_added (GstElement *element,
               GSocket *socket, CustomData *data){
    data->socket = socket;
}

static gboolean background_task (CustomData *data) {
    GSocket *socket = data->socket;
    if(socket == NULL)
        return FALSE;
    g_print("In Backbround task \n");
    int fd = g_socket_get_fd(socket);
    
    struct tcp_info info;
    int infoLen = sizeof(info);
    getsockopt(fd, 6, TCP_INFO, (void *)&info, (socklen_t *)&infoLen);
//    uint64_t throughput = info.tcpi_snd_cwnd * info.tcpi_snd_mss * 8 /1024;
    uint64_t throughput = info.tcpi_feedback_rate * 8;
    if(throughput == 0)
        throughput = info.tcpi_delivery_rate * 8 /1024;
    
    FILE* log = fopen("logfile.log", "a");
    fprintf(log, "Feedback = %lu kbit\n", throughput);
    fclose(log);
    
    g_object_set (data->encoder, "bitrate", throughput, NULL);
    
    return TRUE;
}

int
main (int   argc,
      char *argv[])
{
  GMainLoop *loop;
  CustomData data;

  GstElement *pipeline, *src, *filesrc, *decbin, *vidconv, *filter, *enc, *parser, *dec, *svr, *sink;
//  GstBus *bus;
//  guint bus_watch_id;
  GstCaps *filtercaps;
  GstStateChangeReturn ret;

  /* Initialisation */
  gst_init (&argc, &argv);
  
  
  loop = g_main_loop_new (NULL, FALSE);

  /* Check input arguments */
  if (argc != 2) {
    g_printerr ("Usage: %s <host ip>\n", argv[0]);
    return -1;
  }

  /* Create gstreamer elements */
  pipeline = gst_pipeline_new ("my-pipeline");
  if (!pipeline) {
    g_printerr ("Cannot create pipeline.\n");
    return -1;
  }
      
  //create source element 
  src = gst_element_factory_make ("videotestsrc", "src");
  if (!src) {
    g_printerr ("Cannot create source.\n");
    return -1;
  }
  g_object_set (src, "pattern", 1, NULL); //"snow"=1
  g_object_set (src, "num-buffers", 1800, NULL);
  
  //create file source element 
  filesrc = gst_element_factory_make ("filesrc", "filesrc");
  if (!filesrc) {
    g_printerr ("Cannot create file source.\n");
    return -1;
  }
  g_object_set (filesrc, "location", "bbb.mp4", NULL);
  g_object_set (filesrc, "num-buffers", 6000, NULL);  
  
  //create filter
  filter = gst_element_factory_make ("capsfilter", "filter");
  if (!filter) {
    g_printerr ("Cannot create filter.\n");
    return -1;
  }
  filtercaps = gst_caps_new_simple ("video/x-raw",
               "width", G_TYPE_INT, 512,
               "height", G_TYPE_INT, 340,
               "framerate", GST_TYPE_FRACTION, 30, 1,
               NULL);  
  g_object_set (filter, "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  //Create decode bin and video convert
  decbin = gst_element_factory_make ("decodebin", "decbin");
  if (!decbin) {
    g_printerr ("Cannot create decode bin.\n");
    return -1;
  }
  vidconv = gst_element_factory_make ("videoconvert", "vidconv");
  if (!vidconv) {
    g_printerr ("Cannot create video convert.\n");
    return -1;
  }  
  
  //Create x264 encoder
  enc = gst_element_factory_make ("x264enc", "enc");
  if (!enc) {
    g_printerr ("Cannot create an x264 encoder.\n");
    return -1;
  }  
  g_object_set (enc, "bitrate", 340, NULL);

  //Create x264 parser
  parser = gst_element_factory_make ("h264parse", "parser");
  if (!parser) {
    g_printerr ("Cannot create an x264 parser.\n");
    return -1;
  }    

  //Create x264 decoder
  dec = gst_element_factory_make ("avdec_h264", "dec");
  if (!dec) {
    g_printerr ("Cannot create an x264 decoder.\n");
    return -1;
  }    
  
  //Create TCP server sink
  svr = gst_element_factory_make ("tcpserversink", "svr");
  if (!svr) {
    g_printerr ("Cannot create tcpserversink.\n");
    return -1;
  }
  g_object_set (svr, "host", argv[1], NULL);        
  g_object_set (svr, "port", 8554, NULL);    
  
  sink = gst_element_factory_make ("xvimagesink", "sink");
  if (!sink) {
    g_printerr ("Cannot create sink.\n");
    return -1;
  } 

  /* Initialize our data structure */
  memset (&data, 0, sizeof (data));
  data.loop = loop;
  data.pipeline = pipeline;
  data.source = filesrc;
  data.tcp_svr_sink = svr;
  data.encoder = enc;  
  data.decodebin = decbin;
  
  /* we add all elements into the pipeline */
  gst_bin_add_many (GST_BIN (pipeline), filesrc, decbin, enc, svr, NULL);

  /* we link the elements together */
  //  gst_element_link (src, filter);
  gst_element_link_pads (filesrc, "src", decbin, "sink");
  g_signal_connect (decbin, "pad-added", G_CALLBACK (cb_new_pad), &data);
//  if(!gst_element_link_many (decbin, vidconv, enc, svr, NULL))
//  {
//    g_printerr ("Cannot link elements.\n");
//    return -1;            
//  }

  /* we add a message handler */
//  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
//  gst_bus_add_signal_watch (bus);
//  gst_bus_enable_sync_message_emission (bus);
//  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
//  g_signal_connect (bus, "message", (GCallback) bus_call, bin);
//  g_signal_connect (bus, "sync-message", (GCallback) handle_sync_message, &data);
  g_signal_connect (svr, "client-added", (GCallback)sock_added, &data);
  
 
  /* Set the pipeline to "playing" state*/
  g_print ("Now playing: %s\n", argv[1]);
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (pipeline);
    return -1;
  } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
    data.is_live = TRUE;
  }

  g_timeout_add_seconds (1, (GSourceFunc)background_task, &data);

  /* Iterate */
  g_print ("Running...\n");
  g_main_loop_run (loop);

  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
//  gst_object_unref (bus);
//  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;
}

