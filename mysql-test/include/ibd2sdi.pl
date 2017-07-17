use strict;
use warnings;
use LWP::Simple;
use File::Copy;

my $json_file_path = $ENV{'JSON_FILE_PATH'} or die;
my $dir = $ENV{'MYSQLTEST_VARDIR'} or die;

sub ibd2sdi_replace() {
  # open file in write mode
  open IN_FILE,"<", "$json_file_path" or die $! ;
  open (OUT_FILE, ">:raw", "$dir/tmp/tmpfile") or die $! ;
  while(<IN_FILE>) {

    # Remove number output from "last_altered" value
    $_=~ s/("last_altered": )[0-9]+/$1NNN/g;

    # Remove number output from "created" value
    $_=~ s/("created": )[0-9]+/$1NNN/g;

    # Remove se_private_data: id & trx_id output. retain the root page number value
    $_=~ s/("se_private_data":) "id=[0-9]+;root=[0-9]+;trx_id=[0-9]+;"/$1 "id=X;root=Y;trx_id=Z"/g;

    # Remove se_private_id: id. This is innodb table_id
    $_=~ s/("se_private_id":) [0-9]+/$1NNN/g;

    # Remove object_id output
    $_=~ s/("id": )[0-9]+/$1X/g;

    # Remove file_per_tablespace id output in dd::Table
    $_=~ s/("tablespace_ref": "innodb_file_per_table.)[0-9]+"/$1X"/g;

    # Remove file_per_tablespace id output in dd::Tablespace
    $_=~ s/("name": "innodb_file_per_table.)[0-9]+"/$1X"/g;

    # Remove id output in se_prvate_data of dd::Tablespace
    $_=~ s/("se_private_data": "flags=)([0-9]+)(;id=)[0-9]+;"/$1X$3Y;"/g;

    # Remove mysql version id
    $_=~ s/("mysqld_version_id": )[0-9]+/$1X/g;

    # Remove extra path separator seen on windows and partition names
    if (m/"filename":/)
    {
      $_=~ s/\\\\/\//g;
    }

    $_=~ s/#P#/#p#/g;

    $_=~ s/#SP#/#sp#/g;

    $_=~ s/("default_value": ).*/$1"",/g;

    print OUT_FILE $_;
  }
  close(IN_FILE);
  close(OUT_FILE);
  #move the new content from tmp file to the orginal file.
  move ("$dir/tmp/tmpfile", "$json_file_path");
}

sub ibd2sdi_replace_system() {
  # open file in write mode
  open IN_FILE,"<", "$json_file_path" or die $! ;
  open (OUT_FILE, ">:raw", "$dir/tmp/tmpfile") or die $! ;
  while(<IN_FILE>) {

    # Remove number output from "last_altered" value
    $_=~ s/("last_altered": )[0-9]+/$1NNN/g;

    # Remove number output from "created" value
    $_=~ s/("created": )[0-9]+/$1NNN/g;

    # Remove se_private_data: id & trx_id output. retain the root page number value
    # This is only thing that differed from normal replace
    $_=~ s/("se_private_data":) "id=[0-9]+;root=[0-9]+;trx_id=[0-9]+;"/$1 "id=X;root=Y;trx_id=Z"/g;

    # Remove se_private_id: id. This is innodb table_id
    $_=~ s/("se_private_id":) [0-9]+/$1NNN/g;

    # Remove object_id output
    $_=~ s/("id": )[0-9]+/$1X/g;

    # Remove file_per_tablespace id output in dd::Table
    $_=~ s/("tablespace_ref": "innodb_file_per_table.)[0-9]+"/$1X"/g;

    # Remove file_per_tablespace id output in dd::Tablespace
    $_=~ s/("name": "innodb_file_per_table.)[0-9]+"/$1X"/g;

    # Remove id output in se_prvate_data of dd::Tablespace
    $_=~ s/("se_private_data": "flags=)([0-9]+)(;id=)[0-9]+;"/$1X$3Y;"/g;

    # Remove mysql version id
    $_=~ s/("mysqld_version_id": )[0-9]+/$1X/g;

    # Remove extra path separator seen on windows and partition names
    if (m/"filename":/)
    {
      $_=~ s/\\\\/\//g;
    }

    $_=~ s/#P#/#p#/g;

    $_=~ s/#SP#/#sp#/g;

    $_=~ s/("default_value": ).*/$1"",/g;

    print OUT_FILE $_;
  }
  close(IN_FILE);
  close(OUT_FILE);
  #move the new content from tmp file to the orginal file.
  move ("$dir/tmp/tmpfile", "$json_file_path");
}

sub ibd2sdi_replace_mysql() {
  # open file in write mode
  open IN_FILE,"<", "$json_file_path" or die $! ;
  open (OUT_FILE, ">:raw", "$dir/tmp/tmpfile") or die $! ;
  while(<IN_FILE>) {

    # Remove number output from "last_altered" value
    $_=~ s/("last_altered": )[0-9]+/$1NNN/g;

    # Remove number output from "created" value
    $_=~ s/("created": )[0-9]+/$1NNN/g;

    # Remove se_private_data: id & trx_id output. retain the root page number value
    $_=~ s/("se_private_data":) "id=[0-9]+;root=[0-9]+;trx_id=[0-9]+;"/$1 "id=X;root=Y;trx_id=Z"/g;

    # Remove se_private_id: id. This is innodb table_id
    $_=~ s/("se_private_id":) [0-9]+/$1NNN/g;

    # Remove object_id output
    $_=~ s/("id": )[0-9]+/$1X/g;

    # Remove file_per_tablespace id output in dd::Table
    $_=~ s/("tablespace_ref": "innodb_file_per_table.)[0-9]+"/$1X"/g;

    # Remove file_per_tablespace id output in dd::Tablespace
    $_=~ s/("name": "innodb_file_per_table.)[0-9]+"/$1X"/g;

    # Remove id output in se_prvate_data of dd::Tablespace
    $_=~ s/("se_private_data": "flags=)([0-9]+)(;id=)[0-9]+;"/$1X$3Y;"/g;

    # Remove mysql version id
    $_=~ s/("mysqld_version_id": )[0-9]+/$1X/g;

    # Remove extra path separator seen on windows and partition names
    if (m/"filename":/)
    {
      $_=~ s/\\\\/\//g;
    }

    $_=~ s/#P#/#p#/g;

    $_=~ s/#SP#/#sp#/g;

    $_=~ s/("default_value": ).*/$1"",/g;

    $_=~ s/("collation_id": )[0-9]+/$1X/g;

    print OUT_FILE $_;
  }

  close(IN_FILE);
  close(OUT_FILE);
  #move the new content from tmp file to the orginal file.
  move ("$dir/tmp/tmpfile", "$json_file_path");
}

# "require ibd2sdi.pl" says the module should return true and this
# achieved by the below statement
1;