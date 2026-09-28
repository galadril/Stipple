# name: Snake
# summary: A snake that plays itself, counts its own free space, and rarely traps itself.
# author: Stipple
# tags: game, animation, generative, classic
# panel: 52x16

import math

# Snake on a 26 x 8 grid of two-pixel cells.
#
# It plays itself, which is the whole point: a game needing the button is a
# blank screen on a shelf, and a snake that dies in four seconds is a blank
# screen with extra steps.
#
# **The flood fill is what makes it watchable.** A snake that simply walks
# towards the food traps itself inside its own tail within about thirty
# apples - reliably, and always in the same dull way. Before each move this
# counts how many cells would still be reachable from each candidate, and
# refuses to walk into a pocket smaller than the snake. That one rule is the
# difference between a demo and something you leave running.
#
# The fill is bounded by construction: it visits each of 208 cells at most
# once per candidate, and there are never more than three candidates. The
# `seen` array is allocated once and stamped with a generation number rather
# than refilled, because clearing 208 entries three times a move is the kind
# of cost that only shows up as a dropped frame.

class App
  var W, H                 # grid, in cells
  var body                 # cell indices, tail first, head last
  var occupied             # W*H flags, so "is this cell snake" is one lookup
  var seen, stamp          # flood fill, reused
  var dir                  # -1 none yet
  var food
  var last, dead, score, best

  def init()
    self.W = 26
    self.H = 8
    self.occupied = []
    self.seen = []
    var i = 0
    while i < self.W * self.H
      self.occupied.push(false)
      self.seen.push(0)
      i += 1
    end
    self.stamp = 0
    self.best = 0
    self._restart()
  end

  def _restart()
    var i = 0
    while i < self.W * self.H
      self.occupied[i] = false
      i += 1
    end
    # Four segments, not one. A single cell is a dot, and the first seconds
    # after a restart are exactly when somebody is looking at it.
    self.body = []
    var start = 4 * self.W + 6
    var k = 0
    while k < 4
      var cell = start + k
      self.body.push(cell)
      self.occupied[cell] = true
      k += 1
    end
    self.dir = 1
    self.dead = false
    self.score = 0
    self.last = 0
    self._drop()
  end

  # Somewhere the snake is not. Tries at random, then walks the grid - a
  # purely random search would take unbounded time once the snake is long,
  # which on a full board is never.
  def _drop()
    var tries = 0
    while tries < 40
      var c = math.rand() % (self.W * self.H)
      if !self.occupied[c]
        self.food = c
        return
      end
      tries += 1
    end
    var i = 0
    while i < self.W * self.H
      if !self.occupied[i]
        self.food = i
        return
      end
      i += 1
    end
    self.food = -1   # board full; nothing left to eat
  end

  # Where a direction lands, or -1 off the edge. Walls are solid rather than
  # wrapping: wrapping makes the flood fill meaningless, because everywhere
  # is reachable from everywhere.
  def _step(cell, d)
    var x = cell % self.W
    var y = cell / self.W
    if d == 0
      y -= 1
    elif d == 1
      x += 1
    elif d == 2
      y += 1
    else
      x -= 1
    end
    if x < 0 || x >= self.W || y < 0 || y >= self.H
      return -1
    end
    return y * self.W + x
  end

  # Reachable cells from here, counted no further than `need`.
  #
  # The cap is the whole trick. The question is never "how much room is
  # there" but "is there room for me", and stopping at the answer turns a
  # 208-cell walk into a 20-cell one on an open board. Counting the whole
  # board three times a move was the first version and it overran the
  # per-frame instruction budget on frame nine.
  #
  # The neighbour walk is written out rather than calling _step, for the same
  # reason Game of Life writes out its eight neighbours: a method call per
  # neighbour is four per cell, and at a few hundred cells that is the
  # difference between fitting in a frame and losing it.
  def _roomAtLeast(from, need)
    self.stamp += 1
    var mark = self.stamp
    var queue = [from]
    self.seen[from] = mark
    var count = 0
    var at = 0
    var W = self.W
    var H = self.H

    while at < size(queue)
      var cell = queue[at]
      at += 1
      count += 1
      if count >= need
        return count
      end

      var x = cell % W
      var y = cell / W

      if y > 0
        var n = cell - W
        if self.seen[n] != mark && !self.occupied[n]
          self.seen[n] = mark
          queue.push(n)
        end
      end
      if y < H - 1
        var n = cell + W
        if self.seen[n] != mark && !self.occupied[n]
          self.seen[n] = mark
          queue.push(n)
        end
      end
      if x > 0
        var n = cell - 1
        if self.seen[n] != mark && !self.occupied[n]
          self.seen[n] = mark
          queue.push(n)
        end
      end
      if x < W - 1
        var n = cell + 1
        if self.seen[n] != mark && !self.occupied[n]
          self.seen[n] = mark
          queue.push(n)
        end
      end
    end
    return count
  end

  def _distance(a, b)
    var ax = a % self.W
    var ay = a / self.W
    var bx = b % self.W
    var by = b / self.W
    var dx = ax - bx
    var dy = ay - by
    if dx < 0 dx = -dx end
    if dy < 0 dy = -dy end
    return dx + dy
  end

  def _think()
    var head = self.body[size(self.body) - 1]
    # Room enough to fit, plus a little. Asking for exactly the body length
    # leaves a snake that fills its pocket precisely and dies on the next
    # apple.
    var need = size(self.body) + 3

    var safeDir = -1
    var safeDist = 9999
    var looseDir = -1
    var looseRoom = -1

    var d = 0
    while d < 4
      # No reversing. With a body longer than one that is instant death, and
      # checking it here is cheaper than discovering it below.
      if (d + 2) % 4 != self.dir
        var next = self._step(head, d)
        if next >= 0 && !self.occupied[next]
          # The tail moves out of the way as the head moves in, so the cell
          # it is vacating counts as free. Without this the snake refuses to
          # follow its own tail, which is the one manoeuvre that keeps it
          # alive on a crowded board.
          var tail = self.body[0]
          self.occupied[tail] = false
          var room = self._roomAtLeast(next, need)
          self.occupied[tail] = true

          if room >= need
            # Safe. Among safe moves the nearest to the food wins - this is
            # the only place the snake is actually playing rather than
            # surviving.
            var dist = self.food >= 0 ? self._distance(next, self.food) : 0
            if dist < safeDist
              safeDir = d
              safeDist = dist
            end
          elif room > looseRoom
            # Nothing is safe. Take the roomiest and hope the tail clears.
            looseDir = d
            looseRoom = room
          end
        end
      end
      d += 1
    end

    return safeDir >= 0 ? safeDir : looseDir
  end

  def _advance()
    var d = self._think()
    if d < 0
      self.dead = true
      if self.score > self.best
        self.best = self.score
      end
      return
    end
    self.dir = d

    var head = self._step(self.body[size(self.body) - 1], d)
    self.body.push(head)
    self.occupied[head] = true

    if head == self.food
      self.score += 1
      self._drop()
    else
      var tail = self.body[0]
      self.occupied[tail] = false
      self.body.remove(0)
    end
  end

  def on_button(name)
    self._restart()
  end

  def draw()
    clear(rgb(0, 0, 0))
    var now = now_ms()

    if self.dead
      # A moment on the score, then away again. Left sitting on "game over"
      # it would be a still image for however long the carousel gives it.
      if now - self.last > 2500
        self._restart()
      end
      text(2, 1, "DEAD", rgb(200, 60, 50))
      text(2, 9, str(self.score), rgb(120, 120, 120))
      if self.best > 0
        var b = str(self.best)
        text(51 - text_width(b), 9, b, rgb(60, 70, 60))
      end
      return
    end

    # Twelve moves a second on the device clock, so it runs at the same speed
    # whatever else the panel is doing.
    if now - self.last >= 80
      self.last = now
      self._advance()
    end

    if self.food >= 0
      rect_fill((self.food % self.W) * 2, (self.food / self.W) * 2, 2, 2,
                rgb(230, 60, 40))
    end

    # Head brightest, fading down the body. The gradient is what turns a line
    # of green squares into something with a direction.
    var n = size(self.body)
    var i = 0
    while i < n
      var cell = self.body[i]
      var fade = (i * 160) / n
      var colour = rgb(20, 80 + fade, 40)
      if i == n - 1
        colour = rgb(200, 255, 200)
      end
      rect_fill((cell % self.W) * 2, (cell / self.W) * 2, 2, 2, colour)
      i += 1
    end
  end
end

return App()
