class Range
  ##
  # call-seq:
  #    rng.first    -> obj
  #    rng.first(n) -> an_array
  #
  # Returns the first object in the range, or an array of the first +n+
  # elements.
  #
  #   (10..20).first     #=> 10
  #   (10..20).first(3)  #=> [10, 11, 12]
  #
  def first(*args)
    raise RangeError, "cannot get the first element of beginless range" if self.begin.nil?
    return self.begin if args.empty?

    raise ArgumentError, "wrong number of arguments (given #{args.length}, expected 1)" unless args.length == 1
    nv = args[0]
    n = nv.__to_int
    raise ArgumentError, "negative array size (or size too big)" unless 0 <= n
    ary = []
    each do |i|
      break if n <= 0
      ary.push(i)
      n -= 1
    end
    ary
  end

  ##
  # call-seq:
  #    rng.last    -> obj
  #    rng.last(n) -> an_array
  #
  # Returns the last object in the range,
  # or an array of the last +n+ elements.
  #
  # Note that with no arguments +last+ will return the object that defines
  # the end of the range even if #exclude_end? is +true+.
  #
  #   (10..20).last      #=> 20
  #   (10...20).last     #=> 20
  #   (10..20).last(3)   #=> [18, 19, 20]
  #   (10...20).last(3)  #=> [17, 18, 19]
  def last(*args)
    raise RangeError, "cannot get the last element of endless range" if self.end.nil?
    return self.end if args.empty?

    raise ArgumentError, "wrong number of arguments (given #{args.length}, expected 1)" unless args.length == 1
    nv = args[0]
    n = nv.__to_int
    raise ArgumentError, "negative array size (or size too big)" unless 0 <= n
    return self.to_a.last(nv)
  end

  ##
  # call-seq:
  #    rng.max                   -> obj
  #    rng.max {|a,b| block }    -> obj
  #
  # Returns the maximum value in the range. Returns nil if the range is empty
  # or excludes its end and the end is not an Integer. For non-numeric ranges
  # or when a block is given, it delegates to Enumerable#max.
  #
  #   (10..20).max      #=> 20
  #   (10...20).max     #=> 19
  #   ('a'..'z').max    #=> "z"
  #
  def max(&block)
    val = self.begin
    last = self.end
    return super(&block) if block

    raise RangeError, "cannot get the maximum of endless range" if last.nil?

    # fast path for numerics
    if val.kind_of?(Numeric) && last.kind_of?(Numeric)
      raise TypeError if exclude_end? && !last.kind_of?(Integer)
      return nil if val > last
      return nil if val == last && exclude_end?

      max = last
      max -= 1 if exclude_end?
      return max
    end

    # delegate to Enumerable
    super()
  end

  ##
  # call-seq:
  #    rng.min                   -> obj
  #    rng.min {|a,b| block }    -> obj
  #
  # Returns the minimum value in the range. For non-numeric ranges or when
  # a block is given, it delegates to Enumerable#min.
  #
  #   (10..20).min      #=> 10
  #   ('a'..'z').min    #=> "a"
  #
  def min(&block)
    val = self.begin
    last = self.end
    if block
      raise RangeError, "cannot get the minimum of endless range with custom comparison method" if last.nil?
      return super(&block)
    end
    return val if last.nil?

    # fast path for numerics
    if val.kind_of?(Numeric) && last.kind_of?(Numeric)
      return nil if val > last
      return nil if val == last && exclude_end?

      min = val
      return min
    end

    # delegate to Enumerable
    super()
  end

  ##
  # call-seq:
  #    rng.overlap?(other_range) -> true or false
  #
  # Returns true if self and other_range have at least one element in common,
  # false otherwise.
  #
  #   (1..5).overlap?(4..6) #=> true
  #   (1..5).overlap?(7..9) #=> false
  #
  def overlap?(other)
    raise TypeError, "argument must be a range" unless other.kind_of?(Range)

    self_begin = self.begin
    other_end = other.end
    other_excl = other.exclude_end?

    return false if __empty_range?(self_begin, other_end, other_excl)

    other_begin = other.begin
    self_end = self.end
    self_excl = self.exclude_end?

    return false if __empty_range?(other_begin, self_end, self_excl)
    return true if self_begin == other_begin

    return false if __empty_range?(self_begin, self_end, self_excl)
    return false if __empty_range?(other_begin, other_end, other_excl)

    true
  end
end
