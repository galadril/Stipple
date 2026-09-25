# name: Aquarium
# summary: Three fish, a sunken chest and a bed of weed. Press to feed them.
# author: Galadril
# tags: ambient, animation, button
# panel: 52x16

# Rebuilt for 52x16 from Galadril's Pixel Aquarium, which was written for a
# 32x8 panel.
#
# Not scaled. On 32x8 the water is six rows deep and everything - fish, weed,
# chest, bubbles - has to share them, so the tank reads as a strip with things
# on it. Sixteen rows is a water column: the sand can have depth, the weed can
# grow tall enough to sway, and the fish can swim at different heights and
# pass each other. That is what the extra pixels buy, so that is what they are
# spent on.

import math

class App
  var fx, fy, fdir, fbody, ftail, fspeed
  var bx, by
  var foodX, foodY, feeding, lastFood
  var lastMove, lastBubble

  def init()
    # Three fish, at three depths, at three speeds. One fish in a tank this
    # size looks lost; three at the same depth look like a shoal of one.
    self.fx    = [6, 30, 18]
    self.fy    = [3, 7, 10]
    self.fdir  = [1, -1, 1]
    self.fbody = [0xFF7A18, 0x00BFFF, 0xFFD060]
    self.ftail = [0xFFB000, 0x0066FF, 0xC08020]
    self.fspeed = [0, 0, 0]

    # Five bubbles, started at different heights so they never rise in step.
    self.bx = [9, 21, 33, 44, 15]
    self.by = [11, 8, 13, 6, 3]

    self.foodX = 26
    self.foodY = 0
    self.feeding = false
    self.lastFood = 0

    self.lastMove = 0
    self.lastBubble = 0
  end

  # Night is quieter and darker. Without a clock it is always day - a tank
  # that decided it was night because the device had not synchronised yet
  # would just look broken.
  def night()
    if !time_known()
      return false
    end
    var h = hour()
    return h >= 22 || h < 7
  end

  def on_button(name)
    # Food lands somewhere in the middle, away from the glass, and every fish
    # goes for it.
    self.foodX = 8 + (math.rand() % 36)
    self.foodY = 0
    self.feeding = true
    self.lastFood = now_ms()
  end

  def step()
    var t = now_ms()

    var speed = 260
    if self.feeding
      speed = 130
    elif self.night()
      speed = 620
    end

    if t - self.lastMove >= speed
      self.lastMove = t
      var i = 0
      while i < 3
        # Feeding overrides wandering: everything turns towards the food.
        if self.feeding
          if self.fx[i] < self.foodX
            self.fdir[i] = 1
          elif self.fx[i] > self.foodX
            self.fdir[i] = -1
          end
        elif (math.rand() % 14) == 0
          self.fdir[i] = -self.fdir[i]
        end

        self.fx[i] = self.fx[i] + self.fdir[i]

        # Turn at the glass rather than swimming through it.
        if self.fx[i] >= width() - 5
          self.fdir[i] = -1
        end
        if self.fx[i] <= 1
          self.fdir[i] = 1
        end

        # A slow vertical drift, so they do not swim along invisible rails.
        if (math.rand() % 20) == 0
          var dy = 1
          if (math.rand() % 2) == 0
            dy = -1
          end
          var ny = self.fy[i] + dy
          if ny >= 2 && ny <= 11
            self.fy[i] = ny
          end
        end
        i += 1
      end
    end

    if t - self.lastBubble >= 240
      self.lastBubble = t
      var b = 0
      while b < 5
        self.by[b] = self.by[b] - 1
        if self.by[b] < 1
          # Back to the sand, somewhere new.
          self.by[b] = 13
          self.bx[b] = 2 + (math.rand() % 48)
        end
        b += 1
      end
    end

    if self.feeding && t - self.lastFood >= 260
      self.lastFood = t
      self.foodY += 1
      if self.foodY >= 13
        self.feeding = false
      else
        var f = 0
        while f < 3
          var dx = self.fx[f] + 2 - self.foodX
          if dx < 0
            dx = -dx
          end
          var dy = self.fy[f] - self.foodY
          if dy < 0
            dy = -dy
          end
          if dx <= 2 && dy <= 1
            self.feeding = false
          end
          f += 1
        end
      end
    end
  end

  def draw_water()
    if self.night()
      clear(0x000610)
    else
      clear(0x001028)
    end

    # The surface: a line that moves, so the top of the tank is not a hard
    # edge. Two pixels of it at a time, drifting - a whole lit row would read
    # as a lid.
    var lit = 0x0A3A5A
    if self.night()
      lit = 0x05202F
    end
    var drift = (now_ms() / 220) % 8
    var x = 0
    while x < width()
      if (x + drift) % 8 < 3
        pixel(x, 0, lit)
      end
      x += 1
    end
  end

  def draw_bed()
    var sand = 0xD6A434
    var dark = 0x8A6325
    var lite = 0xF0C052
    if self.night()
      sand = 0x3A2B12
      dark = 0x261C0C
      lite = 0x4C3A18
    end

    # Two rows, not one. Depth is the point of the taller panel.
    rect_fill(0, 14, width(), 2, sand)

    # Grains, at an irregular spacing so the bed does not look ruled.
    var spots = [3, 9, 14, 22, 27, 35, 41, 48]
    var i = 0
    while i < size(spots)
      pixel(spots[i], 14, dark)
      pixel(spots[i] + 1, 15, lite)
      i += 1
    end
  end

  # One frond, swaying from a fixed root. Tall enough to be worth the motion,
  # which it never was at eight rows.
  def weed(rootX, tall, sway, a, b)
    var y = 13
    var x = rootX
    var n = 0
    while n < tall
      var colour = a
      if n % 2 == 1
        colour = b
      end
      pixel(x, y, colour)
      # The higher up the frond, the further it leans.
      if n >= tall - 3
        x += sway
      end
      y -= 1
      n += 1
    end
  end

  def draw_plants()
    var a = 0x00AA44
    var b = 0x00DD66
    if self.night()
      a = 0x00301A
      b = 0x004A26
    end

    var sway = 1
    if (now_ms() / 600) % 2 == 0
      sway = -1
    end

    self.weed(3, 8, sway, a, b)
    self.weed(6, 5, -sway, b, a)
    self.weed(46, 7, -sway, a, b)
    self.weed(49, 5, sway, b, a)
  end

  def draw_rocks()
    var rock = 0x5A5A5A
    if self.night()
      rock = 0x1C1C1C
    end
    rect_fill(12, 12, 3, 2, rock)
    pixel(13, 11, rock)
    rect_fill(38, 12, 4, 2, rock)
    pixel(39, 11, rock)
  end

  def draw_chest()
    var brown = 0x8B4513
    var gold = 0xFFBB00
    if self.night()
      brown = 0x341A08
      gold = 0x5A3C00
    end

    rect_fill(23, 11, 7, 3, brown)
    rect_fill(23, 11, 7, 1, gold)
    pixel(26, 12, gold)

    # The glint, which only happens now and then. A chest that sparkled every
    # frame would be a lamp.
    if !self.night() && (now_ms() / 1000) % 5 == 0
      pixel(26, 10, 0xFFFFFF)
    end
  end

  def draw_fish(i)
    var x = self.fx[i]
    var y = self.fy[i]
    var body = self.fbody[i]
    var tail = self.ftail[i]

    if self.night()
      body = 0x203040
      tail = 0x152030
    end

    if self.fdir[i] > 0
      pixel(x, y, tail)
      pixel(x, y + 1, tail)
      rect_fill(x + 1, y, 3, 1, body)
      rect_fill(x + 1, y + 1, 2, 1, body)
      pixel(x + 3, y, 0xFFFFFF)
    else
      pixel(x + 4, y, tail)
      pixel(x + 4, y + 1, tail)
      rect_fill(x + 1, y, 3, 1, body)
      rect_fill(x + 2, y + 1, 2, 1, body)
      pixel(x + 1, y, 0xFFFFFF)
    end
  end

  def draw_bubbles()
    var c = 0x44CCFF
    if self.night()
      c = 0x123344
    end
    var i = 0
    while i < 5
      pixel(self.bx[i], self.by[i], c)
      i += 1
    end
  end

  def draw_sleep()
    if !self.night()
      return
    end
    # A Z above the first fish, on and off, so a dark tank still has one thing
    # moving in it.
    if (now_ms() % 2400) < 1400
      var x = self.fx[0] + 2
      var c = 0x446688
      pixel(x, self.fy[0] - 2, c)
      pixel(x + 1, self.fy[0] - 2, c)
      pixel(x, self.fy[0] - 1, c)
      pixel(x + 1, self.fy[0] - 3, c)
    end
  end

  def draw()
    self.step()

    self.draw_water()
    self.draw_bed()
    self.draw_plants()
    self.draw_rocks()
    self.draw_chest()
    self.draw_bubbles()

    var i = 0
    while i < 3
      self.draw_fish(i)
      i += 1
    end

    if self.feeding
      pixel(self.foodX, self.foodY, 0xFF7722)
    end
    self.draw_sleep()
  end
end

return App()
