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
pp r.h
pp r.to_s

# test regex
re = Regexp.new(r.to_s)
pp WORDS.all? { |s| re =~ s }

