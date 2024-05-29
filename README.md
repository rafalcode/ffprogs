tryign to collect all my ffmpeg programming work

the decode audio is the onbly one that reads an audio file some of the example self generate tehir own audi o - nt fair

>>> tmp30.c 

was successful in that it can take an opus file (and other formats too)
and transcode to mp3, genuinely.

However the bit rate was only 96 which is poor (128 could be acceptable).

One trouble is getting the number of frames in input. Beware thoug that ffmpeg usually reserves 
    the word frame for the video, and perhaps packet is what it wants to hear when it's about ZZ

>> transcode via shell or perl?
actually this got a vbr
ffmpeg -i willie.opus -codec:a libmp3lame -qscale:a 2 mvbr.mp3
also some 30% or so bigger than the opus
however considering it's a 138kb/s in opus, perhaps we shouldn't ask for that high quality
change to 5 and you get a more reasonable size. If anything, sligthly lower than opus file.

this appears to be faster too.

>> concat
[ just to mention I was trying to simulate Evolution Control OCmmittee's  California dreamings
sure this exists but it's not as easy as:
ffmpeg -i willie.opus -ss 1:00.0 -t 10.0 -codec:a libmp3lame -qscale:a 5 mvbr3.mp3

nope. you cancheck 
https://trac.ffmpeg.org/wiki/Concatenate
out.

one thing you'll see is that you won't be able to search in eaqch file like that.
so you won't be able to concatenate without creating temporary "snippet" files.

you can create a listing of them, but in this particular manner (i.e not just a simple listing)
file 'mvbr3.mp3'
file 'mvbr4.mp3'

adn then this works well:
ffmpeg -f concat -i l.l -c copy all.mp3

Aother thing you might want to do is slow it down.
the atempo filter is limited to max half speed, but you can append another filter to make it quart speed.
ffmpeg -i willie.opus -ss 60 -t 5 -filter:a "atempo=0.5,atempo=0.5" w0.mp3

by chance I got the "on the road again" bit

However, there seems to be alot of artifacts there ... well, that's sort of natural
slowing down means interpolating what do you expect?
all that is in https://trac.ffmpeg.org/wiki/How%20to%20speed%20up%20/%20slow%20down%20a%20video
