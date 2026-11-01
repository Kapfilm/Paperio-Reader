The progressive_detail and progressive_restart fixtures are lossless JPEG
transcodings of the existing synthetic contrast_420.jpg and odd_420.jpg.
No user book or cover image is included. Generate them with libjpeg-turbo:

    jpegtran -progressive -outfile progressive_detail.jpg contrast_420.jpg
    jpegtran -progressive -restart 1B -outfile progressive_restart.jpg odd_420.jpg
    djpeg -grayscale -outfile progressive_detail.gray.pgm progressive_detail.jpg
    djpeg -grayscale -outfile progressive_restart.gray.pgm progressive_restart.jpg

The grayscale references decode all progressive AC/DC scans at source resolution.
The second file uses restart markers in both interleaved and noninterleaved scans.
