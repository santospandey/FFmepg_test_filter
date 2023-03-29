#include<stdio.h>
#include<libavformat/avformat.h>
#include<libavcodec/avcodec.h>
#include<libavutil/avutil.h>
#include<libavfilter/avfilter.h>

#include<ao/ao.h>

static AVFilterGraph* filter_graph = NULL;

static int init_filter_graph(){
    filter_graph = avfilter_graph_alloc();
    if(!filter_graph){
        fprintf(stderr, "Error allocating filter graph\n");
        return -1;
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

    

    avformat_close_input(&ic);
    avformat_free_context(ic);

    return 0;
}