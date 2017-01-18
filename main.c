#include <gst/gst.h>
#include <glib.h>


static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;

  g_print ("Received a message!!!!!!!\n");
  
  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
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


int
main (int   argc,
      char *argv[])
{
  GMainLoop *loop;

  GstElement *pipeline, *src, *filter, *enc, *svr, *sink;
  GstBus *bus;
  guint bus_watch_id;
  GstCaps *filtercaps;

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
  g_object_set (src, "pattern", "snow", NULL);
  g_object_set (src, "num-buffers", 1800, NULL);
  
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

  //Create x264 encoder
  enc = gst_element_factory_make ("x264enc", "enc");
  if (!enc) {
    g_printerr ("Cannot create an x264 encoder.\n");
    return -1;
  }  
  g_object_set (enc, "bitrate", 512, NULL);

  //Create TCP server sink
  svr = gst_element_factory_make ("tcpserversink", "svr");
  if (!svr) {
    g_printerr ("Cannot create tcpserversink.\n");
    return -1;
  }
  g_object_set (svr, "host", argv[1], NULL);        
  g_object_set (svr, "port", 8554, NULL);    
  
  sink = gst_element_factory_make ("ximagesink", "sink");
  if (!sink) {
    g_printerr ("Cannot create sink.\n");
    return -1;
  } 

  /* we add all elements into the pipeline */
  gst_bin_add_many (GST_BIN (pipeline), src, filter, sink, NULL);

  /* we link the elements together */
//  gst_element_link (src, filter);
  if(!gst_element_link_many (src, filter, sink, NULL))
  {
    g_printerr ("Cannot link elements.\n");
    return -1;            
  }

  /* we add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);
  
  
  /* Set the pipeline to "playing" state*/
  g_print ("Now playing: %s\n", argv[1]);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);


  /* Iterate */
  g_print ("Running...\n");
  g_main_loop_run (loop);


  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;
}

