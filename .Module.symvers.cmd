cmd_/home/daniele/SOA/Module.symvers := sed 's/ko$$/o/' /home/daniele/SOA/modules.order | scripts/mod/modpost  -a  -o /home/daniele/SOA/Module.symvers -e -i Module.symvers  -N -T -
