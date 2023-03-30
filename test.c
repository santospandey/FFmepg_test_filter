#include<stdio.h>
#include<libavformat/avformat.h>
#include<libavcodec/avcodec.h>
#include<libavutil/avutil.h>
#include<libavfilter/avfilter.h>

#include<ao/ao.h>

static AVFilterGraph* filter_graph = NULL;
static AVFilterContext* abuffer_ctx = NULL;
static AVFilterContext* volume_ctx = NULL;
static AVFilterContext* aformat_ctx = NULL;
static AVFilterContext* abuffer_sink_ctx = NULL;
static char args[512];
int arg_size=512;

static int init_filter_graph(AVFormatContext *ic, AVStream *audio_st){
    filter_graph = avfilter_graph_alloc();
    if(!filter_graph){
        fprintf(stderr, "Error allocating filter graph\n");
        return -1;
    }
    
    AVFilter *abuffer = avfilter_get_by_name("abuffer");
    if(!abuffer){
        fprintf(stderr, "Error getting abuffer filter\n");
        return -1;
    }
    AVFilter *volume = avfilter_get_by_name("volume");
    if(!volume){
        fprintf(stderr, "Error getting volume filter\n");
        return -1;
    }
    AVFilter *aformat = avfilter_get_by_name("aformat");
    if(!aformat){
        fprintf(stderr, "Error getting aformat filter\n");
        return -1;
    }
    AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
    if(!abuffersink){
        fprintf(stderr, "Error getting abuffer sink filter\n");
        return -1;
    }

    int err;
    AVCodecContext *codec_ctx = audio_st->codec;
    AVRational timebase = audio_st->time_base;
    snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64, 
    timebase.num, timebase.den, codec_ctx->sample_rate, av_get_sample_fmt_name(codec_ctx->sample_fmt), codec_ctx->channel_layout);
    fprintf(stdout, "abuffer filter => %s\n", args);
    
    // create abuffer filter
    err = avfilter_graph_create_filter(&abuffer_ctx, abuffer, NULL, args, NULL, filter_graph);
    if(err<0){
        fprintf(stderr, "Error creating abuffer filter\n");
        return -1;
    }

    // create volume filter
    double vol = 0.5;
    snprintf(args, sizeof(args), "volume=%f", vol);
    fprintf(stdout, "volume filter => %s\n", args);

    err = avfilter_graph_create_filter(&volume_ctx, volume, NULL, args, NULL, filter_graph);
    if(err<0){
        fprintf(stderr, "Error creating volume filter\n");
        return -1;
    }

    // create aformat filter
    snprintf(args, sizeof(args),
            "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64,
            av_get_sample_fmt_name(AV_SAMPLE_FMT_S16), 44100,
            (uint64_t)AV_CH_LAYOUT_STEREO);
    fprintf(stderr, "aformat: %s\n", args);

    err = avfilter_graph_create_filter(&aformat_ctx, aformat,
            NULL, args, NULL, filter_graph);
    if (err < 0) {
        fprintf(stderr, "Unable to create aformat filter\n");
        return -1;
    }

    // create abuffer sink filter
    err = avfilter_graph_create_filter(&abuffer_sink_ctx, abuffersink, NULL, NULL, NULL, filter_graph);
    if(err < 0){
        fprintf(stderr, "Unable to create abuffersink filter\n");
        return -1;
    }

    // connect inputs and outputs
    err = avfilter_link(abuffer_ctx, 0, volume_ctx, 0);
    if(err >= 0) err = avfilter_link(volume_ctx, 0, aformat_ctx, 0);
    if(err >= 0) err = avfilter_link(aformat_ctx, 0, abuffer_sink_ctx, 0);
    if(err < 0){
        fprintf(stderr, "Error in linking filter \n");
        return -1;
    }

    err = avfilter_graph_config(filter_graph, NULL);
    if(err < 0){
        fprintf(stderr, "Error in configuring filter graph \n");
        return -1;
    }
    
    return 0;
}


static int audio_decode_frame(AVCodecContext *codec_ctx, AVStream *audio_stream, AVPacket *pkt, AVFrame *frame){
    AVPacket pkt_tmp_;
    memset(&pkt_tmp_, 0, sizeof(pkt_tmp_));
    AVPacket *pkt_tmp = &pkt_tmp_;
    
    *pkt_tmp = *pkt;

    int ret = avcodec_send_packet(codec_ctx, frame);
    if(ret < 0){
        fprintf(stderr, "Error in sending ")
    }


}


int main(int argc, char* argv[]){
    fflush(stderr);
    if(argc < 2){        
        fprintf(stderr, "Usage %s filename \n", argv[0]);
        return 1;
    }

    AVFormatContext *ic = avformat_alloc_context();
    if(!ic){
        fprintf(stderr, "Error in allocation format context");
        return 1;
    }
    
    if(avformat_open_input(&ic, argv[1], NULL, NULL) != 0){
        fprintf(stderr, "Error in open input stream\n");
        return 1;
    }
    
    if(avformat_find_stream_info(ic, NULL) < 0){
        fprintf(stderr, "Error in reading media file to get stream info\n");
        return 1;
    }

    fprintf(stdout, "Format %s, Duration %ld, bitrate %ld \n", ic->iformat->name, ic->duration, ic->bit_rate);

    AVCodec *decoder = NULL;
    int audio_index = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if(audio_index < 0){
        fprintf(stderr, "Error in find audio stream index\n");
        return 1;
    }
    fprintf(stdout, "Audio index %d \n", audio_index);
    
    if(!decoder){
        fprintf(stderr, "Error no decoder found \n");
        return 1;
    }

    AVStream *audio_stream = ic->streams[audio_index];
    AVCodecContext *audio_codec_ctx = audio_stream->codec;

    if(avcodec_open2(audio_codec_ctx, decoder, NULL) != 0){
        fprintf(stderr, "Error in initilizing codec context \n");
        return 1;
    }

    if(!audio_codec_ctx->channel_layout){
        audio_codec_ctx->channel_layout = av_get_default_channel_layout(audio_codec_ctx->channels);        
    }
    if(!audio_codec_ctx->channel_layout){
        fprintf(stderr, "Error in finding channel layout");
        return 1;
    }

    int status = init_filter_graph(ic, audio_stream);
    if(status < 0){
        fprintf(stderr, "Error in initialization filter\n");
        return 1;
    }

    AVPacket audio_pkt;
    memset(&audio_pkt, 0, sizeof(audio_pkt));
    AVPacket *pkt = &audio_pkt;
    AVFrame *frame = av_frame_alloc();
    if(!frame){
        fprintf(stderr, "Error in allocating frame\n");
        return 1;
    }

    AVFrame *oframe = av_frame_alloc();
    if(!oframe){
        fprintf(stderr, "Error in allocating output frame\n");
        return 1;
    }

    int eof = 0;
    while(1){
        if(eof){
            if (audio_codec_ctx->codec->capabilities & AV_CODEC_CAP_DELAY) {
                av_init_packet(pkt);
                pkt->data = NULL;
                pkt->size = 0;
                pkt->stream_index = audio_index;
                if (audio_decode_frame(audio_codec_ctx, audio_stream, pkt, frame) > 0) {
                    // keep flushing
                    continue;
                }
            }
            break;
        }

        int err = av_read_frame(ic, pkt);
        if(err < 0){
            if(err != AVERROR_EOF){
                fprintf(stderr, "Error in reading frame \n");                
            }
            eof = 1;
            continue;
        }
        if(pkt->stream_index != audio_index){
            av_free_packet(pkt);
            continue;
        }
        audio_decode_frame(audio_codec_ctx, audio_stream, pkt, frame);
        av_free_packet(pkt);
    }



    avformat_close_input(&ic);
    avformat_free_context(ic);

    return 0;
}