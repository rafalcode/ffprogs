/*
 * Copyright (c) 2013-2022 Andreas Unterweger
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file audio transcoding to MPEG/AAC API usage example
 * @example transcode_aac.c
 *
 * Convert an input audio file to AAC in an MP4 container. Formats other than
 * MP4 are supported based on the output file extension.
 * @author Andreas Unterweger (dustsigns@gmail.com)
 */

#include <stdio.h>

#include <libavutil/mem.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>

#include <libavcodec/avcodec.h>

#include <libavutil/audio_fifo.h>
#include <libavutil/avassert.h>
#include <libavutil/avstring.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>

#include <libswresample/swresample.h>

/* The output bit rate in bit/s */
#define OUTPUT_BIT_RATE 96000
/* The number of output channels */
#define OUTPUT_CHANNELS 2

/* Global timestamp for the audio frames. */
static int64_t pts = 0;

/**
 * Open an input file and the required decoder.
 * @param      filename             File to be opened
 * @param[out] inpfcx Format context of opened file
 * @param[out] inpccx  Codec context of opened file
 * @return Error code (0 if successful)
 */
static int open_input_file(const char *filename, AVFormatContext **inpfcx, AVCodecContext **inpccx)
{
    AVCodecContext *avctx;
    const AVCodec *input_codec;
    const AVStream *stream;
    int error;

    /* Open the input file to read from it. */
    if ((error = avformat_open_input(inpfcx, filename, NULL,
                                     NULL)) < 0) {
        fprintf(stderr, "Could not open input file '%s' (error '%s')\n",
                filename, av_err2str(error));
        *inpfcx = NULL;
        return error;
    }

    /* Get information on the input file (number of streams etc.). */
    if ((error = avformat_find_stream_info(*inpfcx, NULL)) < 0) {
        fprintf(stderr, "Could not open find stream info (error '%s')\n",
                av_err2str(error));
        avformat_close_input(inpfcx);
        return error;
    }

    /* Make sure that there is only one stream in the input file. */
    if ((*inpfcx)->nb_streams != 1) {
        fprintf(stderr, "Expected one audio input stream, but found %d\n",
                (*inpfcx)->nb_streams);
        avformat_close_input(inpfcx);
        return AVERROR_EXIT;
    }

    stream = (*inpfcx)->streams[0];

    /* Find a decoder for the audio stream. */
    if (!(input_codec = avcodec_find_decoder(stream->codecpar->codec_id))) {
        fprintf(stderr, "Could not find input codec\n");
        avformat_close_input(inpfcx);
        return AVERROR_EXIT;
    }

    /* Allocate a new decoding context. */
    avctx = avcodec_alloc_context3(input_codec);
    if (!avctx) {
        fprintf(stderr, "Could not allocate a decoding context\n");
        avformat_close_input(inpfcx);
        return AVERROR(ENOMEM);
    }

    /* Initialize the stream parameters with demuxer information. */
    error = avcodec_parameters_to_context(avctx, stream->codecpar);
    if (error < 0) {
        avformat_close_input(inpfcx);
        avcodec_free_context(&avctx);
        return error;
    }

    /* Open the decoder for the audio stream to use it later. */
    if ((error = avcodec_open2(avctx, input_codec, NULL)) < 0) {
        fprintf(stderr, "Could not open input codec (error '%s')\n",
                av_err2str(error));
        avcodec_free_context(&avctx);
        avformat_close_input(inpfcx);
        return error;
    }

    /* Set the packet timebase for the decoder. */
    avctx->pkt_timebase = stream->time_base;

    /* Save the decoder context for easier access later. */
    *inpccx = avctx;

    return 0;
}

/**
 * Open an output file and the required encoder.
 * Also set some basic encoder parameters.
 * Some of these parameters are based on the input file's parameters.
 * @param      filename              File to be opened
 * @param      inpccx   Codec context of input file
 * @param[out] outfcx Format context of output file
 * @param[out] outccx  Codec context of output file
 * @return Error code (0 if successful)
 */
static int open_output_file(const char *filename, AVCodecContext *inpccx, AVFormatContext **outfcx, AVCodecContext **outccx)
{
    AVCodecContext *avctx          = NULL;
    AVIOContext *output_io_context = NULL;
    AVStream *stream               = NULL;
    const AVCodec *output_codec    = NULL;
    int error;

    /* Open the output file to write to it. */
    if ((error = avio_open(&output_io_context, filename, AVIO_FLAG_WRITE)) < 0) {
        fprintf(stderr, "Could not open output file '%s' (error '%s')\n", filename, av_err2str(error));
        return error;
    }

    /* Create a new format context for the output container format. */
    if (!(*outfcx = avformat_alloc_context())) {
        fprintf(stderr, "Could not allocate output format context\n");
        return AVERROR(ENOMEM);
    }

    /* Associate the output file (pointer) with the container format context. */
    (*outfcx)->pb = output_io_context;

    /* Guess the desired container format based on the file extension. */
    if (!((*outfcx)->oformat = av_guess_format(NULL, filename, NULL))) {
        fprintf(stderr, "Could not find output file format\n");
        goto cleanup;
    }

    if (!((*outfcx)->url = av_strdup(filename))) {
        fprintf(stderr, "Could not allocate url.\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    /* Find the encoder to be used by its name. */
    if (!(output_codec = avcodec_find_encoder(AV_CODEC_ID_MP3))) {
        fprintf(stderr, "Could not find an MP3 encoder.\n");
        goto cleanup;
    }

    /* Create a new audio stream in the output file container. */
    if (!(stream = avformat_new_stream(*outfcx, NULL))) {
        fprintf(stderr, "Could not create new stream\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    avctx = avcodec_alloc_context3(output_codec);
    if (!avctx) {
        fprintf(stderr, "Could not allocate an encoding context\n");
        error = AVERROR(ENOMEM);
        goto cleanup;
    }

    /* Set the basic encoder parameters.
     * The input file's sample rate is used to avoid a sample rate conversion. */
    av_channel_layout_default(&avctx->ch_layout, OUTPUT_CHANNELS);
    avctx->sample_rate    = inpccx->sample_rate;
    avctx->sample_fmt     = output_codec->sample_fmts[0];
    avctx->bit_rate       = OUTPUT_BIT_RATE;

    /* Set the sample rate for the container. */
    stream->time_base.den = inpccx->sample_rate;
    stream->time_base.num = 1;

    /* Some container formats (like MP4) require global headers to be present.
     * Mark the encoder so that it behaves accordingly. */
    if ((*outfcx)->oformat->flags & AVFMT_GLOBALHEADER)
        avctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    /* Open the encoder for the audio stream to use it later. */
    if ((error = avcodec_open2(avctx, output_codec, NULL)) < 0) {
        fprintf(stderr, "Could not open output codec (error '%s')\n", av_err2str(error));
        goto cleanup;
    }

    error = avcodec_parameters_from_context(stream->codecpar, avctx);
    if (error < 0) {
        fprintf(stderr, "Could not initialize stream parameters\n");
        goto cleanup;
    }

    /* Save the encoder context for easier access later. */
    *outccx = avctx;

    return 0;

cleanup:
    avcodec_free_context(&avctx);
    avio_closep(&(*outfcx)->pb);
    avformat_free_context(*outfcx);
    *outfcx = NULL;
    return error < 0 ? error : AVERROR_EXIT;
}

/**
 * Initialize one data packet for reading or writing.
 * @param[out] packet Packet to be initialized
 * @return Error code (0 if successful)
 */
static int init_packet(AVPacket **packet)
{
    if (!(*packet = av_packet_alloc())) {
        fprintf(stderr, "Could not allocate packet\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Initialize one audio frame for reading from the input file.
 * @param[out] frame Frame to be initialized
 * @return Error code (0 if successful)
 */
static int init_input_frame(AVFrame **frame)
{
    if (!(*frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate input frame\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Initialize the audio resampler based on the input and output codec settings.
 * If the input and output sample formats differ, a conversion is required
 * libswresample takes care of this, but requires initialization.
 * @param      inpccx  Codec context of the input file
 * @param      outccx Codec context of the output file
 * @param[out] resccx     Resample context for the required conversion
 * @return Error code (0 if successful)
 */
static int init_resampler(AVCodecContext *inpccx, AVCodecContext *outccx, SwrContext **resccx)
{
        int error;

        /*
         * Create a resampler context for the conversion.
         * Set the conversion parameters.
         */
        error = swr_alloc_set_opts2(resccx, &outccx->ch_layout, outccx->sample_fmt, outccx->sample_rate,
                                            &inpccx->ch_layout, inpccx->sample_fmt, inpccx->sample_rate, 0, NULL);
        if (error < 0) {
            fprintf(stderr, "Could not allocate resample context\n");
            return error;
        }
        /*
        * Perform a sanity check so that the number of converted samples is
        * not greater than the number of samples to be converted.
        * If the sample rates differ, this case has to be handled differently
        */
        av_assert0(outccx->sample_rate == inpccx->sample_rate);

        /* Open the resampler with the specified parameters. */
        if ((error = swr_init(*resccx)) < 0) {
            fprintf(stderr, "Could not open resample context\n");
            swr_free(resccx);
            return error;
        }
    return 0;
}

/**
 * Initialize a FIFO buffer for the audio samples to be encoded.
 * @param[out] fifo                 Sample buffer
 * @param      outccx Codec context of the output file
 * @return Error code (0 if successful)
 */
static int init_fifo(AVAudioFifo **fifo, AVCodecContext *outccx)
{
    /* Create the FIFO buffer based on the specified output sample format. */
    if (!(*fifo = av_audio_fifo_alloc(outccx->sample_fmt, outccx->ch_layout.nb_channels, 1))) {
        fprintf(stderr, "Could not allocate FIFO\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Write the header of the output file container.
 * @param outfcx Format context of the output file
 * @return Error code (0 if successful)
 */
static int write_output_file_header(AVFormatContext *outfcx) // take a format context (fcx) and wirte out (fcx must clearly contain outfname).
{
    int error;
    if ((error = avformat_write_header(outfcx, NULL)) < 0) {
        fprintf(stderr, "Could not write output file header (error '%s')\n", av_err2str(error));
        return error;
    }
    return 0;
}

/**
 * Decode one audio frame from the input file.
 * @param      frame                Audio frame to be decoded
 * @param      inpfcx Format context of the input file
 * @param      inpccx  Codec context of the input file
 * @param[out] data_present         Indicates whether data has been decoded
 * @param[out] finished             Indicates whether the end of file has
 *                                  been reached and all data has been
 *                                  decoded. If this flag is false, there
 *                                  is more data to be decoded, i.e., this
 *                                  function has to be called again.
 * @return Error code (0 if successful)
 */
static int decode_audio_frame(AVFrame *frame, AVFormatContext *inpfcx, AVCodecContext *inpccx, int *data_present, int *finished)
{
    /* Packet used for temporary storage. */
    AVPacket *input_packet;

    int error = init_packet(&input_packet);
    if (error < 0)
        return error;

    *data_present = 0;
    *finished = 0;
    /* Read one audio frame from the input file (fcx) into a temporary packet. */
    if ((error = av_read_frame(inpfcx, input_packet)) < 0) {
        /* If we are at the end of the file, flush the decoder below. */
        if (error == AVERROR_EOF)
            *finished = 1;
        else {
            fprintf(stderr, "Could not read frame (error '%s')\n", av_err2str(error));
            goto cleanup;
        }
    }

    /* Send the audio frame stored in the temporary packet to the decoder.
     * The input audio stream decoder is used to do this. */
    if ((error = avcodec_send_packet(inpccx, input_packet)) < 0) {
        fprintf(stderr, "Could not send packet for decoding (error '%s')\n", av_err2str(error));
        goto cleanup;
    }

    /* Receive one frame from the decoder. */
    error = avcodec_receive_frame(inpccx, frame);
    /* If the decoder asks for more data to be able to decode a frame,
     * return indicating that no data is present. */
    if (error == AVERROR(EAGAIN)) {
        error = 0;
        goto cleanup;
    /* If the end of the input file is reached, stop decoding. */
    } else if (error == AVERROR_EOF) {
        *finished = 1;
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        fprintf(stderr, "Could not decode frame (error '%s')\n",
                av_err2str(error));
        goto cleanup;
    /* Default case: Return decoded data. */
    } else {
        *data_present = 1;
        goto cleanup;
    }

cleanup:
    av_packet_free(&input_packet);
    return error;
}

/**
 * Initialize a temporary storage for the specified number of audio samples.
 * The conversion requires temporary storage due to the different format.
 * The number of audio samples to be allocated is specified in frame_size.
 * @param[out] converted_input_samples Array of converted samples. The
 *                                     dimensions are reference, channel
 *                                     (for multi-channel audio), sample.
 * @param      outccx    Codec context of the output file
 * @param      frame_size              Number of samples to be converted in
 *                                     each round
 * @return Error code (0 if successful)
 */
static int init_converted_samples(uint8_t ***conv_isamps, AVCodecContext *outccx, int frame_size)
{
    int error;

    /* Allocate as many pointers as there are audio channels.
     * Each pointer will point to the audio samples of the corresponding
     * channels (although it may be NULL for interleaved formats).
     * Allocate memory for the samples of all channels in one consecutive
     * block for convenience. */
    if ((error = av_samples_alloc_array_and_samples(conv_isamps, NULL, outccx->ch_layout.nb_channels, frame_size, outccx->sample_fmt, 0)) < 0) {
        fprintf(stderr, "Could not allocate converted input samples (error '%s')\n", av_err2str(error));
        return error;
    }
    return 0;
}

/**
 * Convert the input audio samples into the output sample format.
 * The conversion happens on a per-frame basis, the size of which is
 * specified by frame_size.
 * @param      input_data       Samples to be decoded. The dimensions are
 *                              channel (for multi-channel audio), sample.
 * @param[out] converted_data   Converted samples. The dimensions are channel
 *                              (for multi-channel audio), sample.
 * @param      frame_size       Number of samples to be converted
 * @param      resccx Resample context for the conversion
 * @return Error code (0 if successful)
 */
static int convert_samples(const uint8_t **input_data, uint8_t **converted_data, const int frame_size, SwrContext *resccx)
{
    int error;

    /* Convert the samples using the resampler. */
    if ((error = swr_convert(resccx, converted_data, frame_size, input_data, frame_size)) < 0) {
        fprintf(stderr, "Could not convert input samples (error '%s')\n", av_err2str(error));
        return error;
    }

    return 0;
}

/**
 * Add converted input audio samples to the FIFO buffer for later processing.
 * @param fifo                    Buffer to add the samples to
 * @param converted_input_samples Samples to be added. The dimensions are channel
 *                                (for multi-channel audio), sample.
 * @param frame_size              Number of samples to be converted
 * @return Error code (0 if successful)
 */
static int add_samples_to_fifo(AVAudioFifo *fifo, uint8_t **converted_input_samples, const int frame_size)
{
    int error;

    /* Make the FIFO as large as it needs to be to hold both,
     * the old and the new samples. */
    if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + frame_size)) < 0) {
        fprintf(stderr, "Could not reallocate FIFO\n");
        return error;
    }

    /* Store the new samples in the FIFO buffer. */
    if (av_audio_fifo_write(fifo, (void **)converted_input_samples, frame_size) < frame_size) {
        fprintf(stderr, "Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }
    return 0;
}

/**
 * Read one audio frame from the input file, decode, convert and store
 * it in the FIFO buffer.
 * @param      fifo                 Buffer used for temporary storage
 * @param      inpfcx Format context of the input file
 * @param      inpccx  Codec context of the input file
 * @param      outccx Codec context of the output file
 * @param      resampler_context    Resample context for the conversion
 * @param[out] finished             Indicates whether the end of file has
 *                                  been reached and all data has been
 *                                  decoded. If this flag is false,
 *                                  there is more data to be decoded,
 *                                  i.e., this function has to be called
 *                                  again.
 * @return Error code (0 if successful)
 */
static int read_decode_convert_and_store(AVAudioFifo *fifo, AVFormatContext *inpfcx, AVCodecContext *inpccx, AVCodecContext *outccx, SwrContext *resampler_context, int *finished)
{
    int ret = AVERROR_EXIT;

    /* Temporary storage of the input samples of the frame read from the file. */
    AVFrame *input_frame = NULL;
    /* Initialize temporary storage for one input frame. */
    if (init_input_frame(&input_frame))
        goto cleanup;

    /* Decode one frame worth of audio samples. */
    int data_present;
    if (decode_audio_frame(input_frame, inpfcx, inpccx, &data_present, finished))
        goto cleanup;

    /* If we are at the end of the file and there are no more samples
     * in the decoder which are delayed, we are actually finished.
     * This must not be treated as an error. */
    if (*finished) {
        ret = 0;
        goto cleanup;
    }

    /* Temporary storage for the converted input samples. */
    uint8_t **conv_isamps = NULL; // converted_input_samples
    /* If there is decoded data, convert and store it. */
    if (data_present) {
        /* Initialize the temporary storage for the converted input samples. */
        if (init_converted_samples(&conv_isamps, outccx, input_frame->nb_samples))
            goto cleanup;

        /* Convert the input samples to the desired output sample format.
         * This requires a temporary storage provided by converted_input_samples. */
        if (convert_samples((const uint8_t**)input_frame->extended_data, conv_isamps, input_frame->nb_samples, resampler_context))
            goto cleanup;

        /* Add the converted input samples to the FIFO buffer for later processing. */
        if (add_samples_to_fifo(fifo, conv_isamps, input_frame->nb_samples))
            goto cleanup;
        ret = 0;
    }
    ret = 0;

cleanup:
    if (conv_isamps)
        av_freep(&conv_isamps[0]);
    av_freep(&conv_isamps);
    av_frame_free(&input_frame);

    return ret;
}

/**
 * Initialize one input frame for writing to the output file.
 * The frame will be exactly frame_size samples large.
 * @param[out] frame                Frame to be initialized
 * @param      outccx Codec context of the output file
 * @param      frame_size           Size of the frame
 * @return Error code (0 if successful)
 */
static int init_output_frame(AVFrame **frame, AVCodecContext *outccx, int frame_size)
{
    int error;

    /* Create a new frame to store the audio samples. */
    if (!(*frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate output frame\n");
        return AVERROR_EXIT;
    }

    /* Set the frame's parameters, especially its size and format.
     * av_frame_get_buffer needs this to allocate memory for the
     * audio samples of the frame.
     * Default channel layouts based on the number of channels
     * are assumed for simplicity. */
    (*frame)->nb_samples     = frame_size;
    av_channel_layout_copy(&(*frame)->ch_layout, &outccx->ch_layout);
    (*frame)->format         = outccx->sample_fmt;
    (*frame)->sample_rate    = outccx->sample_rate;

    /* Allocate the samples of the created frame. This call will make
     * sure that the audio frame can hold as many samples as specified. */
    if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
        fprintf(stderr, "Could not allocate output frame samples (error '%s')\n", av_err2str(error));
        av_frame_free(frame);
        return error;
    }

    return 0;
}

/**
 * Encode one frame worth of audio to the output file.
 * @param      frame                 Samples to be encoded
 * @param      outfcx Format context of the output file
 * @param      outccx  Codec context of the output file
 * @param[out] data_present          Indicates whether data has been
 *                                   encoded
 * @return Error code (0 if successful)
 */
static int encode_audio_frame(AVFrame *frame, AVFormatContext *outfcx, AVCodecContext *outccx, int *data_present)
{
    /* Packet used for temporary storage. */
    AVPacket *output_packet;
    int error;

    error = init_packet(&output_packet);
    if (error < 0)
        return error;

    /* Set a timestamp based on the sample rate for the container. */
    if (frame) {
        frame->pts = pts;
        pts += frame->nb_samples;
    }

    *data_present = 0;
    /* Send the audio frame stored in the temporary packet to the encoder.
     * The output audio stream encoder is used to do this. */
    error = avcodec_send_frame(outccx, frame);
    /* Check for errors, but proceed with fetching encoded samples if the
     *  encoder signals that it has nothing more to encode. */
    if (error < 0 && error != AVERROR_EOF) {
      fprintf(stderr, "Could not send packet for encoding (error '%s')\n", av_err2str(error));
      goto cleanup;
    }

    /* Receive one encoded frame from the encoder. */
    error = avcodec_receive_packet(outccx, output_packet);
    /* If the encoder asks for more data to be able to provide an
     * encoded frame, return indicating that no data is present. */
    if (error == AVERROR(EAGAIN)) {
        error = 0;
        goto cleanup;
    /* If the last frame has been encoded, stop encoding. */
    } else if (error == AVERROR_EOF) {
        error = 0;
        goto cleanup;
    } else if (error < 0) {
        fprintf(stderr, "Could not encode frame (error '%s')\n", av_err2str(error));
        goto cleanup;
    /* Default case: Return encoded data. */
    } else {
        *data_present = 1;
    }

    /* Write one audio frame from the temporary packet to the output file. */
    if (*data_present &&
        (error = av_write_frame(outfcx, output_packet)) < 0) {
        fprintf(stderr, "Could not write frame (error '%s')\n", av_err2str(error));
        goto cleanup;
    }

cleanup:
    av_packet_free(&output_packet);
    return error;
}

/**
 * Load one audio frame from the FIFO buffer, encode and write it to the
 * output file.
 * @param fifo                  Buffer used for temporary storage
 * @param outfcx Format context of the output file
 * @param outccx  Codec context of the output file
 * @return Error code (0 if successful)
 */
static int load_encode_and_write(AVAudioFifo *fifo, AVFormatContext *outfcx, AVCodecContext *outccx)
{
    /* Temporary storage of the output samples of the frame written to the file. */
    AVFrame *output_frame;
    /* Use the maximum number of possible samples per frame.
     * If there is less than the maximum possible frame size in the FIFO
     * buffer use this number. Otherwise, use the maximum possible frame size. */
    const int frame_size = FFMIN(av_audio_fifo_size(fifo),
                                 outccx->frame_size);
    int data_written;

    /* Initialize temporary storage for one output frame. */
    if (init_output_frame(&output_frame, outccx, frame_size))
        return AVERROR_EXIT;

    /* Read as many samples from the FIFO buffer as required to fill the frame.
     * The samples are stored in the frame temporarily. */
    if (av_audio_fifo_read(fifo, (void **)output_frame->data, frame_size) < frame_size) {
        fprintf(stderr, "Could not read data from FIFO\n");
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }

    /* Encode one frame worth of audio samples. */
    if (encode_audio_frame(output_frame, outfcx,
                           outccx, &data_written)) {
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }
    av_frame_free(&output_frame);
    return 0;
}

/**
 * Write the trailer of the output file container.
 * @param outfcx Format context of the output file
 * @return Error code (0 if successful)
 */
static int write_output_file_trailer(AVFormatContext *outfcx)
{
    int error;
    if ((error = av_write_trailer(outfcx)) < 0) {
        fprintf(stderr, "Could not write output file trailer (error '%s')\n",
                av_err2str(error));
        return error;
    }
    return 0;
}

int main(int argc, char **argv)
{
    AVFormatContext *inpfcx = NULL, *outfcx = NULL;
    AVCodecContext *inpccx = NULL, *outccx = NULL;
    SwrContext *resccx = NULL;
    AVAudioFifo *fifo = NULL;
    int ret = AVERROR_EXIT;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(1);
    }

    /* Open the input file for reading. */
    if (open_input_file(argv[1], &inpfcx, &inpccx))
        goto cleanup;

    /* Open the output file for writing. */
    if (open_output_file(argv[2], inpccx, &outfcx, &outccx))
        goto cleanup;

    /* Initialize the resampler to be able to convert audio sample formats. */
    if (init_resampler(inpccx, outccx, &resccx))
        goto cleanup;

    /* Initialize the FIFO buffer to store audio samples to be encoded. */
    if (init_fifo(&fifo, outccx))
        goto cleanup;

    /* Write the header of the output file container. */
    if (write_output_file_header(outfcx))
        goto cleanup;

    /* Loop as long as we have input samples to read or output samples
     * to write; abort as soon as we have neither. */
    unsigned outlooptimes=0;
    while (1) {
        /* Use the encoder's desired frame size for processing. */
        const int output_frame_size = outccx->frame_size;
        int finished = 0;

        /* Make sure that there is one frame worth of samples in the FIFO
         * buffer so that the encoder can do its work.
         * Since the decoder's and the encoder's frame size may differ, we
         * need to FIFO buffer to store as many frames worth of input samples
         * that they make up at least one frame worth of output samples. */
        while (av_audio_fifo_size(fifo) < output_frame_size) {
            /* Decode one frame worth of audio samples, convert it to the
             * output sample format and put it into the FIFO buffer. */
            if (read_decode_convert_and_store(fifo, inpfcx, inpccx, outccx, resccx, &finished))
                goto cleanup;

            /* If we are at the end of the input file, we continue
             * encoding the remaining audio samples to the output file. */
            if (finished)
                break;
        }

        /* If we have enough samples for the encoder, we encode them.
         * At the end of the file, we pass the remaining samples to
         * the encoder. */
        while (av_audio_fifo_size(fifo) >= output_frame_size || (finished && av_audio_fifo_size(fifo) > 0)) {
            /* Take one frame worth of audio samples from the FIFO buffer,
             * encode it and write it to the output file. */
            // if(outlooptimes<4000) {
            //     outlooptimes++;
            //     continue;
            // }
            if (load_encode_and_write(fifo, outfcx, outccx))
                goto cleanup;
        }

        /* If we are at the end of the input file and have encoded
         * all remaining samples, we can exit this loop and finish. */
        if (finished) {
            int data_written;
            /* Flush the encoder as it may have delayed frames. */
            do {
                if (encode_audio_frame(NULL, outfcx, outccx, &data_written))
                    goto cleanup;
            } while (data_written);
            break;
        }
        outlooptimes++;
    } //end of while(1)
    printf("outer loop, how many times? %u\n", outlooptimes);

    /* Write the trailer of the output file container. */
    if (write_output_file_trailer(outfcx))
        goto cleanup;
    ret = 0;

cleanup:
    if (fifo)
        av_audio_fifo_free(fifo);
    swr_free(&resccx);
    if (outccx)
        avcodec_free_context(&outccx);
    if (outfcx) {
        avio_closep(&outfcx->pb);
        avformat_free_context(outfcx);
    }
    if (inpccx)
        avcodec_free_context(&inpccx);
    if (inpfcx)
        avformat_close_input(&inpfcx);

    return ret;
}