/* Reference https://github.com/andrewrk/libavfilter-example/blob/master/main.c */

#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <unistd.h>

#include <ao/ao.h>

static ao_device *device = NULL;

static char strbuf[512];
static AVFilterGraph *filter_graph = NULL;
static AVFilterContext *abuffer_ctx = NULL;
static AVFilterContext *adelay_ctx = NULL;
static AVFilterContext *aformat_ctx = NULL;
static AVFilterContext *abuffersink_ctx = NULL;

static AVFrame *oframe = NULL;

static int init_filter_graph(AVFormatContext *ic, AVStream *input_stream) {
    // create new graph
    filter_graph = avfilter_graph_alloc();
    if (!filter_graph) {
        av_log(NULL, AV_LOG_ERROR, "unable to create filter graph: out of memory\n");
        sleep(5);
        return -1;
    }

    AVFilter *abuffer = avfilter_get_by_name("abuffer");
    AVFilter *volume = avfilter_get_by_name("adelay");
    AVFilter *abuffersink = avfilter_get_by_name("abuffersink");

    int err;
    // create abuffer filter
    AVCodecContext *decoder_ctx= input_stream->codec;
    AVRational time_base = input_stream->time_base;
    snprintf(strbuf, sizeof(strbuf),
            "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64, 
            time_base.num, time_base.den, decoder_ctx->sample_rate,
            av_get_sample_fmt_name(decoder_ctx->sample_fmt),
            decoder_ctx->channel_layout);
    fprintf(stderr, "abuffer: %s\n", strbuf);
    err = avfilter_graph_create_filter(&abuffer_ctx, abuffer,
            NULL, strbuf, NULL, filter_graph);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "error initializing abuffer filter\n");
        return err;
    }
    // create delay filter
    int delay = 5000;
    snprintf(strbuf, sizeof(strbuf), "%d|%d", delay,delay);
    fprintf(stderr, "delay: %s\n", strbuf);
    err = avfilter_graph_create_filter(&adelay_ctx, volume, NULL,strbuf, NULL, filter_graph);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "error initializing volume filter\n");
        sleep(5);
        return err;
    }
 
    // create abuffersink filter
    err = avfilter_graph_create_filter(&abuffersink_ctx, abuffersink,
            NULL, NULL, NULL, filter_graph);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "unable to create aformat filter\n");
        sleep(5);
        return err;
    }

    // connect inputs and outputs
    if (err >= 0) err = avfilter_link(abuffer_ctx, 0, adelay_ctx, 0);
    if (err >= 0) err = avfilter_link(adelay_ctx, 0, abuffersink_ctx, 0);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "error connecting filters\n");
        sleep(5);
        return err;
    }
    err = avfilter_graph_config(filter_graph, NULL);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "error configuring the filter graph\n");
        sleep(5);
        return err;
    }
    return 0;
}

static int audio_decode_frame(AVFormatContext *output_ctx, AVStream *input_stream,AVPacket *pkt, AVFrame *frame,AVCodecContext *encoder_ctx)
{
  
        // push the audio data from decoded frame into the filtergraph
        int err = av_buffersrc_write_frame(abuffer_ctx, frame);
        if (err < 0) {
            fprintf(stderr, "error writing frame to buffersrc\n");
            sleep(5);
            return -1;
        }
        // pull filtered audio from the filtergraph
        for (;;) {
            int err = av_buffersink_get_frame(abuffersink_ctx, oframe);
            if (err == AVERROR_EOF || err == AVERROR(EAGAIN))
                break;
            if (err < 0)
            {
                fprintf(stderr,"error reading buffer from buffersink\n");
                
                return -1;
            }
            // int nb_channels = av_get_channel_layout_nb_channels(oframe->channel_layout);
            // int bytes_per_sample = av_get_bytes_per_sample(oframe->format);
            // int data_size = oframe->nb_samples * nb_channels * bytes_per_sample;
            AVPacket *packet=av_packet_alloc();   
            int ret=0;
            ret = avcodec_send_frame(encoder_ctx, oframe);
            if (ret < 0) 
            {
                fprintf(stderr,"Error sending the frame to the encoder\n");
            }
            while (ret >= 0) 
            {
            ret = avcodec_receive_packet(encoder_ctx, pkt);
                
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return;
            else if (ret < 0)
            {
                fprintf(stderr,"Error encoding audio frame\n");
                break;
            }
            av_interleaved_write_frame(output_ctx,packet);

            }
        av_packet_unref(packet);
        }
        return 0;
}

int main(int argc, char *argv[]) 
{
    ao_initialize();
    avcodec_register_all();
    av_register_all();
    avformat_network_init();
    avfilter_register_all();

    AVFormatContext *input_context = NULL;
    AVCodecContext *decoder_ctx=NULL;
    AVStream *input_stream=NULL;
    AVCodec *decoder = NULL;
    AVFormatContext *output_ctx = NULL;
    int err=0;
    int audio_stream_index = 0;
    ao_sample_format fmt;

    memset(&fmt, 0, sizeof(fmt));
    fmt.bits = 16;
    fmt.channels = 2;
    fmt.rate = 44100;
    fmt.byte_format = AO_FMT_NATIVE;
    char *input_file = "./samples/1.mp3";
    if (avformat_open_input(&input_context, input_file, NULL, NULL) < 0) 
    {
        fprintf(stderr,  "error opening %s\n", input_file);
        return 1;
    }
    if (avformat_find_stream_info(input_context, NULL) < 0) 
    {
        fprintf(stderr,  "%s: could not find codec parameters\n", input_file);
        return 1;
    }
    // set all streams to discard. in a few lines here we will find the audio
    // stream and cancel discarding it
    for (int i = 0; i < input_context->nb_streams; i++)
    {
        input_context->streams[i]->discard = AVDISCARD_ALL;
    }        
    audio_stream_index = av_find_best_stream(input_context, AVMEDIA_TYPE_AUDIO, -1, -1,&decoder, 0);
    if (audio_stream_index < 0) 
    {
       fprintf(stderr, "%s: no audio stream found\n", input_context->filename);
        return 1;
    }
    if (!decoder) 
    {
        fprintf(stderr,  "%s: no decoder found\n", input_context->filename);
        printf("NO decoder found\n");
        sleep(10);
        return 1;
    }
    
    input_stream = input_context->streams[audio_stream_index];
    input_stream->discard = AVDISCARD_DEFAULT;
    decoder_ctx= input_stream->codec;
    if (avcodec_open2(decoder_ctx, decoder, NULL) < 0) {
       fprintf(stderr, "unable to open decoder\n");
       sleep(10);
        return 1;
    }
    if (!decoder_ctx->channel_layout)
    {
        decoder_ctx->channel_layout = av_get_default_channel_layout(decoder_ctx->channels);
    }
    if (!decoder_ctx->channel_layout) 
    {
        fprintf(stderr, "unable to guess channel layout\n");     
        sleep(10);
         return 1;
    }
      printf("Debug point 1\n");
    if (init_filter_graph(input_context, input_stream) < 0) 
    {
        fprintf(stderr, "unable to init filter graph\n");
         sleep(10);
         return 1;
    }
    //output
    int ret=0;
    char *output_file = "final_1.mp3";
    AVOutputFormat *output_fmt = av_guess_format(NULL, output_file , NULL);
    if (!output_fmt)
    {
        fprintf(stderr,"Failed to guess output format\n");
         sleep(10);
    }
    if ((ret = avformat_alloc_output_context2(&output_ctx, output_fmt, NULL, output_file )) < 0)
    {
        fprintf(stderr,"Failed to     allocate output context\n");
         sleep(10);
    }
    output_ctx->oformat->audio_codec = AV_CODEC_ID_MP3;

    // add various params to output context
    AVStream *stream_enc = input_context->streams[0];
    AVCodecParameters *enc_codec_par = stream_enc->codecpar;
    AVCodecContext *encoder_ctx;
    AVStream *out_stream = avformat_new_stream(output_ctx, NULL);
    if (!out_stream)
    {
        fprintf(stderr,"Failed to create output stream\n");
         sleep(10);
    }
    AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!encoder)
    {
        fprintf(stderr,"Failed to find encoder for output format\n");
         sleep(10);
    }
    encoder_ctx = avcodec_alloc_context3(encoder);
    if (!encoder)
    {
        fprintf(stderr,"Failed to allocate encoder codec context\n");
         sleep(10);
    }
    if ((ret = avcodec_parameters_to_context(encoder_ctx, enc_codec_par)) < 0)
    {
        fprintf(stderr,"Failed to set codec context parameters\n");
         sleep(10);
    }

    if ((ret = avcodec_open2(encoder_ctx, encoder, NULL)) < 0)
    {
        fprintf(stderr,"Failed to open codec\n");
         sleep(10);
    }
    out_stream->codecpar->codec_tag = 0;
    if ((ret = avcodec_parameters_from_context(out_stream->codecpar, encoder_ctx)) < 0)
    {
        fprintf(stderr,"Failed to set output codec parameters\n");
         sleep(10);
    }
    out_stream->time_base = stream_enc->time_base;
    if (!(output_fmt->flags & AVFMT_NOFILE))
    {
        if ((ret = avio_open(&output_ctx->pb, output_file , AVIO_FLAG_WRITE)) < 0)
        {
            fprintf(stderr,"Failed to open output file\n");
             sleep(10);
        }
    }
    // Write header
    if ((ret = avformat_write_header(output_ctx, NULL)) < 0)
    {
        fprintf(stderr,"Failed to write output file header\n");
         sleep(10);
    }
  
    AVPacket audio_pkt;
    memset(&audio_pkt, 0, sizeof(audio_pkt));
    AVPacket *pkt = &audio_pkt;
    AVFrame *frame = av_frame_alloc();
    oframe = av_frame_alloc();
    if (!oframe) 
    {
        fprintf(stderr,"error allocating oframe\n");
         sleep(10);
        return 1;
    }
    int eof = 0;
    for (;;) 
    {
        if (eof) 
        {
            if (decoder_ctx->codec->capabilities & AV_CODEC_CAP_DELAY) 
            {
                av_init_packet(pkt);
                pkt->data = NULL;
                pkt->size = 0;
                pkt->stream_index = audio_stream_index;
                if (audio_decode_frame(output_ctx, input_stream, pkt, frame,encoder_ctx) > 0) 
                {
                    // keep flushing
                    continue;
                }
            }
            break;
        }
        err = av_read_frame(input_context, pkt);
        if (err < 0) 
        {
            if (err != AVERROR_EOF)
                fprintf(stderr,"error reading frames\n");
                 sleep(10);
            eof = 1;
            continue;
        }
        if (pkt->stream_index != audio_stream_index) 
        {
            av_free_packet(pkt);
            continue;
        }
        audio_decode_frame(output_ctx, input_stream, pkt, frame,encoder_ctx) ;
        av_free_packet(pkt);
    }
    ret = av_write_trailer(output_ctx);
    if (ret < 0)
    {
        fprintf(stderr, "[Write Trailer],failed\n");
         sleep(10);
    } 
    avformat_network_deinit();
    ao_close(device);
    ao_shutdown();
    return 0;
}