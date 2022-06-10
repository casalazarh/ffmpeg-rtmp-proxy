//based on https://ffmpeg.org/doxygen/trunk/remuxing_8c-example.html
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <time.h>
#include <unistd.h>
#include <curl/curl.h>
int mux();
int mediaLive();
int main(int argc, char **argv)
{

    if (argc < 5) {
    printf("You need to pass at least five parameters.\n");
    return -1;
  }

    const char *in_filename, *out_filename, *timeout, *channel_id;
  in_filename  = argv[1];
  out_filename = argv[2];
  timeout =argv[3];
  channel_id = argv[4];


  while (1) {

      AVFormatContext *input_format_context = NULL, *output_format_context = NULL;
      AVPacket packet;
      AVDictionary *in_opt= NULL;
      AVDictionary *out_opt= NULL;
      int ret, i;
      int stream_index = 0;
      int *streams_list = NULL;
      int number_of_streams = 0;

      // First request
      ret = mux(in_filename, out_filename, timeout,channel_id);
      if (ret == -60){ // -60  represents "Operation timed out"
          fprintf(stderr, "Capturando el evento de timeout y apagando medialive");
          mediaLive(1,channel_id);

      }
      else if (ret == -161){

          fprintf(stderr, "Timeout por output, esperando a que medialive se encienda");

      }
      else if (ret == -5){ // -5 represents "Input/output error"

          printf(stderr,"lógica cuando se desconecta de la fuente");

      }
      //fprintf(stderr, "Error occurred: %d, %s\n", ret, av_err2str(ret));
  }

  return 0;
}


int mux(const char *in_filename, const char *out_filename, const char *timeout, const char *channel_id){

    AVFormatContext *input_format_context = NULL, *output_format_context = NULL;
    AVPacket packet;
    AVDictionary *in_opt= NULL;
    AVDictionary *out_opt= NULL;
    int ret, i;
    int stream_index = 0;
    int *streams_list = NULL;
    int number_of_streams = 0;

    av_dict_set(&in_opt, "f", "flv", 0);
    av_dict_set(&in_opt, "listen", "1", 0);
    av_dict_set(&in_opt, "timeout", timeout, 0);

    if ((ret = avformat_open_input(&input_format_context, in_filename, NULL, &in_opt)) < 0) {
        fprintf(stderr, "Could not open input file '%s'", in_filename);
        goto end;
    }
    av_dict_free(&in_opt);

    if (ret==0){

        fprintf(stderr, "Lógica para iniciar el medialive \n");

        mediaLive(0,channel_id);
    }

    if ((ret = avformat_find_stream_info(input_format_context, NULL)) < 0) {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }


    avformat_alloc_output_context2(&output_format_context, NULL, "flv", out_filename);
    if (!output_format_context) {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    number_of_streams = input_format_context->nb_streams;
    streams_list = av_mallocz_array(number_of_streams, sizeof(*streams_list));

    if (!streams_list) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    for (i = 0; i < input_format_context->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = input_format_context->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;
        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            streams_list[i] = -1;
            continue;
        }
        streams_list[i] = stream_index++;
        out_stream = avformat_new_stream(output_format_context, NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }
        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy codec parameters\n");
            goto end;
        }
    }
    // https://ffmpeg.org/doxygen/trunk/group__lavf__misc.html#gae2645941f2dc779c307eb6314fd39f10
    av_dump_format(output_format_context, 0, out_filename, 1);

    // unless it's a no file (we'll talk later about that) write to the disk (FLAG_WRITE)
    // but basically it's a way to save the file to a buffer so you can store it
    // wherever you want.
    if (!(output_format_context->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&output_format_context->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", out_filename);
            ret=-161;
            goto end;
        }
    }
    AVDictionary* opts = NULL;

    av_dict_set(&out_opt, "f", "flv", 0);

    // https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga18b7b10bb5b94c4842de18166bc677cb
    ret = avformat_write_header(output_format_context, &out_opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }
    while (1) {
        AVStream *in_stream, *out_stream;
        ret = av_read_frame(input_format_context, &packet);
        if (ret < 0)
            break;
        in_stream  = input_format_context->streams[packet.stream_index];
        if (packet.stream_index >= number_of_streams || streams_list[packet.stream_index] < 0) {
            av_packet_unref(&packet);
            continue;
        }
        packet.stream_index = streams_list[packet.stream_index];
        out_stream = output_format_context->streams[packet.stream_index];
        /* copy packet */
        packet.pts = av_rescale_q_rnd(packet.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        packet.dts = av_rescale_q_rnd(packet.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
        packet.duration = av_rescale_q(packet.duration, in_stream->time_base, out_stream->time_base);
        // https://ffmpeg.org/doxygen/trunk/structAVPacket.html#ab5793d8195cf4789dfb3913b7a693903
        packet.pos = -1;

        //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga37352ed2c63493c38219d935e71db6c1
        ret = av_interleaved_write_frame(output_format_context, &packet);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_packet_unref(&packet);
    }
    //https://ffmpeg.org/doxygen/trunk/group__lavf__encoding.html#ga7f14007e7dc8f481f054b21614dfec13
    av_write_trailer(output_format_context);
    end:
    avformat_close_input(&input_format_context);
    /* close output */
    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_format_context->pb);
    avformat_free_context(output_format_context);
    av_freep(&streams_list);
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return ret;
    }
    return 0;
}

int mediaLive(int action, const char *channel_id){
CURL *curl;
CURLcode res;

/* In windows, this will init the winsock stuff */
curl_global_init(CURL_GLOBAL_ALL);

/* get a curl handle */
curl = curl_easy_init();
if(curl) {
/* First set the URL that is about to receive our POST. This URL can
   just as well be a https:// URL if that is what should receive the
   data. */

//curl_easy_setopt(curl, CURLOPT_URL, "https://3sxbs74v30.execute-api.us-west-2.amazonaws.com/PROD");
/* Now specify the POST data */

    const char *text_start, *text_stop;
    char *new;
    
    //text_start= "https:/<apigateway-endpoint>PROD?action=start&channelId=";
    //text_stop=  "apigateway-endpoint/PROD?action=stop&channelId=";
    
if(action==0){
    fprintf(stderr, "Starting Medialive\n");

    new=malloc(strlen(text_start)+1+7);
    strcpy(new, text_start);
    strcat(new,channel_id);

    //fprintf(stderr, "texto =%s--\n",new);
//curl_easy_setopt(curl, CURLOPT_FIELDS, "action=start&channelId=6260948");
    curl_easy_setopt(curl, CURLOPT_URL, new);
}
if(action==1){
    fprintf(stderr, "Stopping Medialive\n");

    new=malloc(strlen(text_stop)+1+7);
    strcpy(new, text_stop);
    strcat(new,channel_id);

    curl_easy_setopt(curl, CURLOPT_URL, new);

//    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "action=stop&channelId=6260948");
    }

    free(new);

/* Perform the request, res will get the return code */
res = curl_easy_perform(curl);
/* Check for errors */
if(res != CURLE_OK)
fprintf(stderr, "curl_easy_perform() failed: %s\n",
curl_easy_strerror(res));

/* always cleanup */
curl_easy_cleanup(curl);
}
curl_global_cleanup();
return 0;
}


