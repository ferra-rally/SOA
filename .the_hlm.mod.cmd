cmd_/home/daniele/SOA/SOA/the_hlm.mod := printf '%s\n'   hlm.o | awk '!x[$$0]++ { print("/home/daniele/SOA/SOA/"$$0) }' > /home/daniele/SOA/SOA/the_hlm.mod
