cmd_/home/daniele/SOA/SOA/the_hlm.o := ld -m elf_x86_64 -z noexecstack --no-warn-rwx-segments   -r -o /home/daniele/SOA/SOA/the_hlm.o @/home/daniele/SOA/SOA/the_hlm.mod  ; ./tools/objtool/objtool  --hacks=jump_label  --hacks=noinstr  --ibt   --orc  --retpoline  --rethunk  --sls   --static-call  --uaccess  --link  --module  /home/daniele/SOA/SOA/the_hlm.o

/home/daniele/SOA/SOA/the_hlm.o: $(wildcard ./tools/objtool/objtool)
