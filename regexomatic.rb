#
# Generate an efficient regular expression from a set of strings.
#
# Example:
#
#   # test words
#   WORDS = %w{foo bar baz blum flob}
#
#   # create instance
#   r = Regexomatic.new
#
#   # add test words
#   WORDS.each { |s| r << s }
#
#   # generate and print regex
#   puts r.to_s
#
class Regexomatic
  #
  # internal state (read-only, used for testing)
  #
  attr_reader :h

  #
  # Create a new Regexomatic instance.
  #
  def initialize
    @h = new_hash
  end

  #
  # Add a string to the set of string.
  #
  def <<(s)
    s.split(//).reduce(@h) { |r, c| r[c] }
    s
  end

  #
  # Add all of the words from an input file, stripping leading and
  # trailing whitespace.
  #
  def add_file(path)
    File.readlines(path).each { |s| self << s.strip }
    nil
  end

  #
  # Generate a regular expression for the given words.
  #
  def to_s
    '\\A' << h_to_s(@h) << '\\Z'
  end

  private

  def new_hash
    Hash.new { |h, k| h[k] = new_hash }
  end

  def h_to_s(h)
    case h.size
    when 0
      ''
    when 1
      c = h.keys.first
      Regexp.quote(c) << h_to_s(h[c])
    else
      s = h.keys.sort.map { |c| Regexp.quote(c) << h_to_s(h[c]) } * '|'
      '(?:' << s << ')'
    end
  end
end
