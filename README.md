**flcarve-project** - In development




I thought about uploading the project when it was finished, but I have discovered that it is an excellent practice among programmers or developers to upload projects and update them frequently. 

Although I might feel a bit embarrassed in case something is not right or optimal, it will also allow me to continue working with the Git workflow.

<details>
<summary><b>Click here to see the last approach</b></summary>
<br>
At the point where we are, although we still need to implement the logic where if we find the gzip signature (the magic bytes) of 0x1F and 0x8B, we would still need to check the third and fourth byte, with the third being the compression method and the fourth byte would be the flags (like FNAME, if it has any comment...).

Having found or polished that block of code, the big question arises: How would we find the footer? Knowing that the last 8 bytes of gzip correspond to the crc32 method and ISIZE (4 bytes for each one), and knowing that the gzip footer position is dynamic rather than static (as its exact location depends on the compressed data size), things can get a bit complicated here.

The options I am considering, although I think I have an idea of which one I will choose, are these:

Option 1: Real-time streaming using the inflate() function, passing the byte stream in real-time to the zlib library. The exact moment the decompression finishes correctly, zlib will return the constant Z_STREAM_END, ensuring we avoid false positives. Although it really catches my attention and I consider it the best, the next option is probably the one I will choose after documenting myself ( but it's not sure yet). 


Option 2: would be through maximum size estimation. I could add to the code that we could pass an argument to the binary which would be the --max-bytes=X flag, and the way in which the system administrator could find the estimated byte size in logrotate (by default or changed manually). This option catches my attention because by not depending on libraries, the utility would remain 100% pure C and free of external dependencies, a final static and portable binary, allowing an analyst to execute it on legacy systems or very restrictive incident response environments where it is not possible to install (for whatever reason) the zlib library.


Option 3: would be to parse the deflate structure. Knowing that the content of a gzip is divided into deflate blocks, each block starts with 3 header bits. The first bit indicates if it is the last block (BFINAL = 1). Although it requires reading bit by bit and complex mathematical formulas since we would be dealing with Huffman codification. Reading bit by bit, after my first project, is not something I am passionate about knowing that, in case of a large file, the pure computational processing would put the CPU to consume a lot of resources.


Finally, a less elegant option although it is viable (but I consider it dangerous and it is almost discarded), would simply be to see where we find the next signature or the next magic bytes of the next compressed file. We would know that the current one has finished, although as I say, I consider it dangerous, but in places where you analyze logs for example or manage systems, the probability that there are deleted logs that have not been overwritten yet is high. Then we would only have to run the zcat utility on the recovered log so that it leaves us a clean log (for example).I will be uploading commits as I continue to document myself, because, to be honest, I spend most of my time taking notes instead of writing code.
</details>

Latest update: 

I have been thinking about the project and documenting myself. I discovered (among other things) that you can embed the zlib library into the binary when you compile it with the static flag (gcc -static main.c -lz -o flcarve), which solved the problem raised in point 2. It is also the industry standard, if I am not mistaken. This has triggered two ideas: we are going to build the project using option 1 as the base, and I will create a second branch inside the repository for option 2, named "experiment-entropy". There, for pure fun and without the pressure of it having to be perfect, I will try to program Shannon's entropy algorithm. With both projects done, we will see which one is more viable or efficient.

In this second option, the check would not rely solely and exclusively on entropy (just in case, by pure randomness, the level within the 0 to 8 scale drops as well, although it shouldn't). If we know the maximum size of a log before being compressed (50MB, 100MB, or whatever it is), we will set that maximum to stop the data extraction if we ever hit that point. In that step, we should simply use zcat or the corresponding tool on the recovered log.

Before moving on to a real problem that I still don't know how to approach, I want to comment on something. Shannon's entropy measures the degree of "surprise", disorder, or randomness of information on a scale from 0 to 8 (8 being the maximum per byte). In a normal log, bytes repeat constantly (letters, spaces, IP addresses, or words like ERROR). Since the data is predictable, the entropy is low. But when we talk about gzip, things change. The goal of a compression algorithm (like Deflate) is to eliminate redundancy to save space. It replaces repetitive patterns with shorter codes. By removing all repetition, the resulting bytes appear mathematically random. That is why its entropy is close to 8.

Here is the other problem I am considering and don't know how to approach or handle yet: disk fragmentation. What happens if the disk is heavily utilized and the log appears fragmented? What way is there to recover a log like that? Or, on the contrary, what happens if an attacker decides to fragment the compressed file when deleting it or overwrites the disk with randomized zeros and ones? Furthermore, it is highly likely that metadata is destroyed as well.

