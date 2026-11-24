// Shared local presentation state; never serialized or replicated. SDK EULA.
	bool					weaponWheelActive;
	int						weaponWheelHoveredSlot;
	int						weaponWheelLastMouseX;
	int						weaponWheelLastMouseY;
	int						weaponWheelLastUpdateTime;
	float					weaponWheelBlend;
	idVec2					weaponWheelCursor;
	idAngles				weaponWheelLastCmdAngles;

	idUserInterface*        weaponWheelGui;
	bool                    weaponWheelGuiActive;
	int                     weaponWheelGuiSlot;
	bool                    weaponWheelCanceled;
	int                     weaponWheelOpenedTime;
	bool                    weaponWheelDebug;
	bool                    weaponWheelDebugHeld;
	void                    InitWeaponWheel();
	void                    FreeWeaponWheelGui();
	// Loads the local player's wheel as the player spawns, inside the level load.
	void                    PrecacheWeaponWheelGui();
	idStr                   RetainedWeaponWheelPath() const;
	bool                    CanUseWeaponWheel( void );
	int                     GetWeaponWheelSlotCount() const;
	bool                    DrawRetainedWeaponWheel();
	void                    DrawLegacyWeaponWheel();
	void					ApplyWeaponWheelInputMask( void );
	void					ResetWeaponWheel( bool instantRestore = false );
	void					UpdateWeaponWheel( void );
	void					UpdateWeaponWheelEffects( void );
	void					UpdateWeaponWheelCursor( void );
	// A weapon control while the wheel is open chooses on it; true when used.
	bool					HandleWeaponWheelImpulse( int impulse );
	void					DrawWeaponWheel( void );
	int						GetWeaponWheelSlotForWeapon( int weaponIndex ) const;
	int						GetWeaponWheelWeaponForSlot( int slot ) const;
	bool					HasWeaponWheelSlotWeapon( int slot ) const;
	int						GetDefaultWeaponWheelSlot( void ) const;
	int						GetClosestAvailableWeaponWheelSlot( float angle ) const;
	int						GetWeaponWheelAmmoCount( int weaponIndex );
	bool					IsWeaponWheelSlotLowAmmo( int slot );
