// openQ4: the multiplayer quick-chat wheel (ui-visual-design.md 14.6, 13.5).
// Local presentation state; never serialized or replicated. SDK EULA.
	bool					quickChatActive;
	bool					quickChatTeam;			// the channel: the team, else everyone
	bool					quickChatCanceled;
	bool					quickChatZoomHeld;
	bool					quickChatDebug;
	bool					quickChatDebugHeld;
	int						quickChatSelected;		// the phrase under the selector, -1 none
	int						quickChatOpenedTime;
	int						quickChatLastUpdateTime;
	int						quickChatLastMouseX;
	int						quickChatLastMouseY;
	float					quickChatBlend;
	idVec2					quickChatCursor;
	idAngles				quickChatLastCmdAngles;

	idUserInterface*		quickChatGui;
	bool					quickChatGuiActive;
	bool					quickChatGuiFailed;
	int						quickChatGuiSelected;
	int						quickChatGuiChannel;
	void					InitQuickChat( void );
	void					FreeQuickChatGui( void );
	// Loads the local player's quick chat as the player spawns.
	void					PrecacheQuickChatGui( void );
	bool					CanUseQuickChat( void );
	void					ApplyQuickChatInputMask( void );
	void					ResetQuickChat( bool instantRestore = true );
	void					UpdateQuickChat( void );
	void					DrawQuickChat( void );
	float					GetQuickChatBlend( void ) const;
