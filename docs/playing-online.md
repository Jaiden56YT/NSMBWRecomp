# Playing NSMBW online with friends

Up to 4 players, each on their own computer with their own controller or keyboard. One person
**hosts**; everyone else **joins**. You all connect through **Hamachi**, a free app that puts
your computers on one private network over the internet.

## What everyone needs

- **Your own copy of the game** (dumped from your own disc) and **the same build of this game as
  the host.** Copies on different versions refuse to connect.
- **Hamachi** (free, from vpn.net), signed in.
- **A controller or keyboard,** set up in the game: press **F10 → Controls**.
- **60 fps:** **F10 → Graphics → Frame interpolation: Off** (it keeps everyone's game smooth
  online).

## One-time setup

1. **Install Hamachi** from vpn.net and sign in (a free LogMeIn account).
2. **Join the host's Hamachi network:** in Hamachi, **Network → Join an existing network**, with
   the network name and password the host gives you. Wait until the host shows a **green dot**.
3. **Start the game once.** The first time, it may show **"Preparing graphics"** for up to half a
   minute while it gets the graphics ready. That happens only once.
4. **Windows:** when Windows asks about the firewall, click **Allow**, and make sure **Public
   networks** is ticked too (Windows counts Hamachi as a public network).
5. **Mac:** if macOS says the game "can't be opened", go to **System Settings → Privacy &
   Security** and click **Open Anyway**.

## Playing together

**The host:**
1. On the file-select screen, choose **Online** (or press **F10 → Online**), then **Host a room**.
2. The screen shows **"Others join with: ... or Hamachi 25.x.x.x:47100"**. Send everyone the
   **Hamachi** address.
3. When everyone is listed in the room, press **Start the game**.

**Everyone else:**
1. On the file-select screen, choose **Online** (or **F10 → Online**), then **Join a room**.
2. Type the host's Hamachi address (paste works: Cmd/Ctrl+V) and press **Join**.
3. Wait in the room until the host presses Start.

**Then:** every game restarts by itself at the same moment. The **host** picks the mode, the save
file and the number of players. On the **"2 Players" (3, 4) screen, press your confirm button** to
join with your character. From there it plays like normal NSMBW multiplayer.

## Good to know

- If someone's game hitches, everyone waits a moment; the window title says **"Waiting for player
  N..."**.
- **To stop:** **F10 → Online → Leave the session**, or close the window. Either one **ends the
  session for everyone**; to play again, the host opens a new room.
- **Progress made online isn't copied back** to the host's normal save yet: it stays in a
  separate online save. Treat online sessions as their own playthrough for now.
- Friends far away add a little input delay (the game adds a few frames so everyone stays in
  step). It is picked automatically from everyone's connection.

## If something goes wrong

- **"No answer from that address":** check that the host shows a green dot in Hamachi, that you
  typed the Hamachi address (25.x.x.x), and that the host allowed the game through their firewall.
- **It connects but won't start, or a player drops right away:** make sure everyone has exactly the
  same build of the game.
- **"Player N stopped responding":** that player's connection dropped for 20 seconds. Check
  Hamachi and start a new room.
