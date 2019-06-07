#!/usr/bin/env ruby

require 'pp'
require_relative './regexomatic'

# test words
WORDS = %w{foo bar baz blum flob}

# create instance
r = Regexomatic.new

# add test words
WORDS.each { |s| r << s }

# print test words, internal state, and regex
pp WORDS
pp re.h
pp re.to_s

# test regex
re = Regexp.new(re.to_s)
pp WORDS.all? { |s| re =~ s }

