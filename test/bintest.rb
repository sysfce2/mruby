$:.unshift File.dirname(File.dirname(File.expand_path(__FILE__)))
require 'test/assert.rb'

GEMNAME = ""

def cmd_bin(s)
  path = s == "mrbc" ? ENV['MRBCFILE'] : "#{ENV['BUILD_DIR']}/bin/#{s}"
  path = path.sub(/\.exe\z/, "")
  if /mswin(?!ce)|mingw|bccwin/ =~ RbConfig::CONFIG['host_os']
    path = "#{path}.exe".tr("/", "\\")
  end
  path
end

def cmd_list(s)
  path_list = [cmd_bin(s)]

  emu = ENV['EMULATOR']
  path_list.unshift emu if emu && !emu.empty?

  path_list
end

def cmd(s)
  cmd_list(s).join(' ')
end

def shellquote(s)
  case RbConfig::CONFIG['host_os']
  when /mswin(?!ce)|mingw|bccwin/
    "\"#{s}\""
  else
    "'#{s}'"
  end
end

print "bintest - Command Binary Test\n\n"

ARGV.each do |gem|
  case gem
  when '-v'; $mrbtest_verbose = true
  end

  case RbConfig::CONFIG['host_os']
  when /mswin(?!ce)|mingw|bccwin/
    gem = gem.tr('\\', '/')
  end

  Dir["#{gem}/bintest/**/*.rb"].each do |file|
    GEMNAME.replace(File.basename(gem))
    load file
  end
end

exit report
