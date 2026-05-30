**flcarve-project** - In development

I thought about uploading the project when it was finished, but I have discovered that it is an excellent practice among programmers or developers to upload projects and update them frequently. 

Although I might feel a bit embarrassed in case something is not right or optimal, it will also allow me to continue working with the Git workflow.

At the point where we are, although we still need to implement the logic where if we find the gzip signature (the magic bytes) of 0x1F and 0x8B, we would still need to check the third and fourth byte, with the third being the compression method and the fourth byte would be the flags (like FNAME, if it has any comment...).

Having found or polished that block of code, the big question arises: How would we find the footer? Knowing that the last 8 bytes of gzip correspond to the crc32 method and ISIZE (4 bytes for each one), and knowing that the gzip footer position is dynamic rather than static (as its exact location depends on the compressed data size), things can get a bit complicated here.

The options I am considering, although I think I have an idea of which one I will choose, are these:

Option 1: Real-time streaming using the inflate() function, passing the byte stream in real-time to the zlib library. The exact moment the decompression finishes correctly, zlib will return the constant Z_STREAM_END, ensuring we avoid false positives. Although it really catches my attention and I consider it the best, the next option is probably the one I will choose after documenting myself ( but it's not sure yet). 


Option 2: would be through maximum size estimation. I could add to the code that we could pass an argument to the binary which would be the --max-bytes=X flag, and the way in which the system administrator could find the estimated byte size in logrotate (by default or changed manually). This option catches my attention because by not depending on libraries, the utility would remain 100% pure C and free of external dependencies, a final static and portable binary, allowing an analyst to execute it on legacy systems or very restrictive incident response environments where it is not possible to install (for whatever reason) the zlib library.


Option 3: would be to parse the deflate structure. Knowing that the content of a gzip is divided into deflate blocks, each block starts with 3 header bits. The first bit indicates if it is the last block (BFINAL = 1). Although it requires reading bit by bit and complex mathematical formulas since we would be dealing with Huffman codification. Reading bit by bit, after my first project, is not something I am passionate about knowing that, in case of a large file, the pure computational processing would put the CPU to consume a lot of resources.


Finally, a less elegant option although it is viable (but I consider it dangerous and it is almost discarded), would simply be to see where we find the next signature or the next magic bytes of the next compressed file. We would know that the current one has finished, although as I say, I consider it dangerous, but in places where you analyze logs for example or manage systems, the probability that there are deleted logs that have not been overwritten yet is high. Then we would only have to run the zcat utility on the recovered log so that it leaves us a clean log (for example).I will be uploading commits as I continue to document myself, because, to be honest, I spend most of my time taking notes instead of writing code.
