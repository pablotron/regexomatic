#!/usr/bin/env ruby

require_relative './regexomatic'

# create instance
r = Regexomatic.new

# add words from /usr/share/dict/words
r.add_file('/usr/share/dict/words')

# print generated regex (large!)
puts r.to_s
