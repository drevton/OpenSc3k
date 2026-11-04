#ifndef SC3_RESOURCES_RESOURCE_H
#define SC3_RESOURCES_RESOURCE_H

#include <set>

#include "common/sc3_sdk_fwd.h"
#include "common/sc3_string.h"
#include "common/sc3_unknown.h"
#include "graphics/api.h"
#include "resources/module_object.h"

// Installed local resources retain their distinct type and group namespaces.
enum RsLocalResourceKey : u32 {
  kRsAcceleratorResourceType = 0xa2e3d533,
  kRsArchitectAcceleratorGroup = 0xa2e3d56d,
  kRsBuildingDataGroup = 0x57e,
  kRsVehicleProducerAttributesResourceType = 0x625c6cc7,
  kRsVehicleProducerAttributesGroup = 0xa25c6d40,
  kRsTrainProducerAttributesResourceType = 0x22cefd99,
  kRsTrainProducerAttributesGroup = 0x62cefddb,
  kRsAirTrafficAttributesResourceType = 0x62dfccec,
  kRsAirTrafficAttributesGroup = 0x62dfcd34,
};

// Local attribute streams distinguish textual TXT! records from binary BIN! records using these file tags.
enum RsAttributeFileTag : u32 {
  kRsTextAttributeTag = 0x21545854,
  kRsBinaryAttributeTag = 0x214e4942,
};

// The building-group factory and local scheme enumerator share this installed resource type.
enum RsBuildingGroupResourceKey : u32 {
  kRsBuildingGroupResourceType = 0x81f53d09,
};

// Installed dialog artwork separates frame, icon, toolbar-toggle, pie-mask and replacement-preview assets.
enum RsDialogArtwork : u32 {
  kRsToolbarToggleOpaqueImageId = 0x53244586,
  kRsToolbarToggleAtlasImageId = 0x53244588,
  kRsToolbarToggleAlphaImageId = 0x53244589,
  kRsDialogFrameAtlasImageId = 0x62b19ce9,
  kRsMessageBoxButtonImageId = 0xe2b66db8,
  kRsMessageBoxIconAtlasImageId = 0x42e55fd4,
  kRsGenericWindowImageId = 0xe2b14588,
  kRsTipDialogFrameImageId = 0xe2b99e03,
  kRsPieGraphAlphaImageId = 0x2d32710,
  kRsPieGraphShadeImageId = 0x62d32734,
  kRsLayerDialogButtonImageId = 0x45cbc2f3,
  kRsReplacementPreviewBadgeImageId = 0x5a096cfb,
};

// Resource-key type words; layer archive types remain distinct from their interface-ID counterparts.
enum RsResourceType : u32 {
  kRsImageResourceType = 0x62b9da24,
  kRsVehicleAttributesResourceType = 0xc25c2bf7,
  kRsBuildingDataResourceType = 0x207edc0e,
  kRsCityLayerRecordType = 0x206c6e7c,
  kRsDisasterLayerRecordType = 0xe1f6abe2,
};

// Image group shared by the main menu, navigation, date/cash and demand panels.
enum RsMainUiImageGroup : u32 {
  kRsMainUiImageGroup = 0x2f78c6d,
};

// Saved-city summary data, preview image and title share this group.
enum RsCitySummaryKey : u32 {
  kRsCitySummaryGroup = 0x19a6cea1,
  kRsCitySummaryResourceType = 0x26e732e0,
};

// Installed flora, people-producer and building-override records retain their resource namespaces.
enum RsOccupantAttributeKey : u32 {
  kRsBuildingOverrideResourceType = 0x856cd19a,
  kRsFloraAttributesResourceType = 0xffd30c03,
  kRsFloraAttributesGroup = 0x80f48961,
  kRsPeopleProducerAttributesResourceType = 0x21fd6eef,
  kRsPeopleProducerAttributesGroup = 0x21fd8e4c,
};

// Localized text groups for the main game and service/query labels.
enum RsTextGroup : u32 {
  kRsGuiTextGroup = 0x225872fe,
  kRsBuildingCatalogTextGroup = 0x62e69238,
  kRsBuildingTextGroup = 0xa1096a4f,
  kRsCreditsTextGroup = 0x830cdf29,
  kRsMenuTextGroup = 0x03c09aff,
  kRsMessageTextGroup = 0x015e410c,
  kRsNewsMessageTextGroup = 0xc2498f0e,
  kRsAdvisorHelpHeadingTextGroup = 0xa26ec340,
  kRsTipOfDayTextGroup = 0x03c09b1b,
  kRsSupplementalBuildingCatalogTextGroup = 0x1a15d9ee,
  kRsSupplementalBuildingTextGroup = 0x453bbde0,
  kRsSupplementalUiTextGroup = 0x541f07db,
  kRsScenarioScriptTextGroup = 0x444288fd,
  kRsGameTextGroup = 0x029541f4,
  kRsCommonTextGroup = 0x209e6378,
  kRsNewsTextGroup = 0x42c1ed2d,
  kRsDisasterNewsTextGroup = 0x547ba060,
  kRsArchitectTextGroup = 0x63de4715,
  kRsSupplementalGameTextGroup = 0x041f2625,
  kRsArchitectSupplementalTextGroup = 0x9a0884ad,
  kRsCityServiceTextGroup = 0x82e0074c,
};

// Localized Game text instances from the installed IXF catalog.
enum RsGameTextId : u32 {
  kRsGameMeetText = 98,
  kRsGamePetitionerIssuesText = 99,
  kRsGameAdvisorsText = 100,
  kRsGameDiscussText = 117,
  kRsGameOrdinancesText = 118,
  kRsGameTakeActionText = 120,
  kRsGameImpactAnalysisText = 121,
  kRsGameBudgetText = 137,
  kRsGameBudgetHeadingFormatText = 138,
  kRsGameNeighborsMapText = 279,
  kRsGameMonthlyContributionText = 318,
  kRsGamePreferencesText = 334,
  kRsGameSoundEffectsText = 335,
  kRsGameMusicText = 339,
  kRsGameResidentsText = 354,
  kRsGameNatLPopText = 357,
  kRsGameTrafficText = 358,
  kRsGamePollutionText = 359,
  kRsGameCrimeText = 361,
  kRsGamePowerText = 363,
  kRsGameWaterText = 364,
  kRsGameEducationText = 366,
  kRsGameLandValueText = 367,
  kRsGameBorrowedText = 439,
  kRsGameBorrowingLimitText = 440,
  kRsGameConsultFinancialAdvisorText = 441,
  kRsGameBriefingsText = 447,
  kRsGameReviewOrdinancesText = 449,
  kRsGameOrdinanceText = 450,
  kRsGameCurrentMonthlyCostText = 451,
  kRsGameMonthlyTotalText = 452,
  kRsGameCurrentBusinessDealsText = 464,
  kRsGameApprovalRatingText = 466,
  kRsGameResFormatText = 468,
  kRsGameCrimeFormatText = 472,
  kRsGamePollutionFormatText = 473,
  kRsGamePopulationText = 481,
  kRsGamePopulationEducationQuotientFormatText = 482,
  kRsGameWorkforceEducationQuotientFormatText = 483,
  kRsGameEducationQuotientText = 484,
  kRsGameWorkforceText = 485,
  kRsGameResidentAgeText = 486,
  kRsGameGarbageGoesToText = 499,
  kRsGameTotalGarbageProducedAnnuallyTonsText = 504,
  kRsGamePopulationLifeExpectancyYearsText = 505,
  kRsGameWorkforceLifeExpectancyYearsText = 506,
  kRsGameTotalPopulationFormatText = 508,
  kRsGameWorkforceOfPopulationText = 509,
  kRsGameBusinessDealsText = 510,
  kRsGamePerformanceText = 511,
  kRsGameTrafficVisibleText = 512,
  kRsGameClosest3ZoomsText = 513,
  kRsGameOtherAnimationsText = 516,
  kRsGameOnText = 517,
  kRsGameSimsVisibleText = 519,
  kRsGameScreenResolutionText = 523,
  kRsGameAmbientSoundText = 537,
  kRsGameMusicChoicesText = 538,
  kRsGameDisastersText = 541,
  kRsGameAutoBudgetText = 542,
  kRsGameAutoGoToDisastersText = 543,
  kRsGameFileNameText = 546,
  kRsGamePopulationFormatText = 547,
  kRsGameFundsFormatText = 548,
  kRsGameDateFormatText = 549,
  kRsGameStringFormatText = 551,
  kRsGameCityNameText = 553,
  kRsGameMayorNameText = 554,
  kRsGameDifficultyLevelText = 555,
  kRsGameStartDateText = 559,
  kRsGameCitySizeText = 563,
  kRsGameLoansText = 566,
  kRsGameMeetFinancialAdvisorText = 568,
  kRsGameTotalMonthlyContributionText = 569,
  kRsGameFinanceBriefingText = 570,
  kRsGameNewLoanText = 571,
  kRsGame10LoanLimitText = 573,
  kRsGameWouldYouLikeToSaveThisCityText = 592,
  kRsGameReGenerateTerrainText = 596,
  kRsGameNewsTopicsText = 600,
  kRsGameViewCityLayersText = 617,
  kRsGameAcceptLoanText = 639,
  kRsGameDefactoText = 641,
  kRsGameNewCityText = 642,
  kRsGameFundsText = 643,
  kRsGameLookInText = 646,
  kRsGameShowCityStatsText = 647,
  kRsGameTotalElectricityUsageAnnuallyMWHText = 649,
  kRsGameAnnuallyAveragedElectricityUsageByTypeText = 650,
  kRsGameHealthFormatText = 654,
  kRsGameEducationFormatText = 655,
  kRsGameEnable3DSoundsText = 657,
  kRsGameSoundText = 658,
  kRsGameShowTipsAtStartupText = 659,
  kRsGamePreviewHighlightedScoreText = 660,
  kRsGame1000M3Text = 662,
  kRsGame100TonsText = 663,
  kRsGame1000MWHText = 664,
  kRsGameCityText = 666,
  kRsGameDiscussDealText = 668,
  kRsGameSystemInfoText = 678,
  kRsGameApplicationNameText = 679,
  kRsGameApplicationVersionText = 680,
  kRsGameOEMIdentificationText = 682,
  kRsGameSimCityAppDirText = 683,
  kRsGameSystemText = 685,
  kRsGameUserNameText = 686,
  kRsGameOSVersionText = 687,
  kRsGameCPUTypeText = 689,
  kRsGameCPUSpeedText = 690,
  kRsGameSystemMemoryText = 691,
  kRsGameDirectXVersionText = 692,
  kRsGameVideoAdapterText = 693,
  kRsGameCurrentScreenModeText = 694,
  kRsGameVideoMemoryText = 695,
  kRsGameTerrainEditText = 714,
  kRsGamePickFeaturesText = 715,
  kRsGameAdjustParametersText = 716,
  kRsGameAcceptThisTerrainText = 717,
  kRsGameGeneralOptionsText = 720,
  kRsGameUseAsMapText = 724,
  kRsGameShowText = 728,
  kRsGameStarterTownDescriptionText = 731,
  kRsGameLoadingText = 732,
  kRsGameSavingText = 733,
  kRsGameDismissPetitionerText = 734,

  kRsGameCostText = 96,
  kRsGamePoliceText = 146,
  kRsGameFireText = 148,
  kRsGamePublicHealthText = 149,
  kRsGameConstanceLeeText = 342,
  kRsGameMortimerGreenText = 343,
  kRsGameKarenFrawlText = 344,
  kRsGameGusOddmanText = 345,
  kRsGameMariaMontoyaText = 346,
  kRsGameMoeBiehlText = 347,
  kRsGameRandallShoopText = 348,
  kRsGameRoadsText = 429,
  kRsGameMassTransitText = 638,
  kRsGameFinancialOrdinancesText = 700,
  kRsGameTransportationOrdinancesText = 701,
  kRsGameCityPlanningOrdinancesText = 702,
  kRsGameEnvironmentalOrdinancesText = 703,
  kRsGamePublicSafetyOrdinancesText = 704,
  kRsGameHEAOrdinancesText = 705,
  kRsGameUtilitiesOrdinancesText = 706,
  kRsGameFinancialBriefingsText = 707,
  kRsGameTransportationBriefingsText = 708,
  kRsGameCityPlanningBriefingsText = 709,
  kRsGameEnvironmentalBriefingsText = 710,
  kRsGamePublicSafetyBriefingsText = 711,
  kRsGameHEABriefingsText = 712,
  kRsGameUtilitiesBriefingsText = 713,

};

// Localized Common text instances from the installed IXF catalog.
enum RsCommonTextId : u32 {
  kRsCommonSimCity3000Text = 160,
  kRsCommonUnknownFileTypeText = 293,
  kRsCommonTheFileSpecifiedOnTheCommandLineText = 294,
  kRsCommonUnknownVideoCardTypeText = 1700,
  kRsCommonWindowsUUFormatText = 1702,
  kRsCommonNonIntelText = 1723,
  kRsCommonUMhzFormatText = 1725,
  kRsCommonCPUFormatText = 1726,
  kRsCommonMMXAbsentText = 1728,
  kRsCommonTimeStampAbsentText = 1732,
  kRsCommonUnknownUserText = 1733,
  kRsCommonWindows98Text = 1735,
  kRsCommonTotalUMBTexturesUMBFormatText = 1736,
  kRsCommonUUFormatText = 1748,
  kRsCommonUMBFormatText = 1750,
  kRsCommonNEEDSC3STRINGTABLEAPPSTRINGText = 4294967295,

  kRsCommonNONameWrongStringText = 550,

};

// Service-query resources use paired label/value rows or five-entry rating rows; these strides are not text IDs.
enum RsCityServiceRowStride : u32 {
  kRsCityServiceRatingRowStride = 5,
  kRsCityServiceLabelValueRowStride = 2,
};

// Localized CityService text instances from the installed IXF catalog.
enum RsCityServiceTextId : u32 {
  kRsCityServiceCleanUpCostFormatText = 50,
  kRsCityServiceLibraryBookCountFormatText = 213,
  kRsCityServiceLibraryReaderCountFormatText = 215,
  kRsCityServiceLibraryGradeLabelText = 216,
  kRsCityServiceSchoolCapacityFormatText = 180,
  kRsCityServiceSchoolStudentsFormatText = 182,
  kRsCityServiceSchoolTeachersFormatText = 184,
  kRsCityServiceSchoolGradeLabelText = 185,
  kRsCityServiceEducationBenefitFormatText = 192,
  kRsCityServiceCollegeCapacityFormatText = 201,
  kRsCityServiceCollegeStudentsFormatText = 203,
  kRsCityServiceCollegeProfessorsFormatText = 205,
  kRsCityServiceCollegeGradeLabelText = 206,
  kRsCityServiceDoctorsFormatText = 172,
  kRsCityServicePatientsFormatText = 170,
  kRsCityServiceHospitalCapacityFormatText = 168,
  kRsCityServiceHospitalGradeLabelText = 173,
  kRsCityServiceMuseumGradeLabelText = 228,
  kRsCityServiceDinosaurBonesFormatText = 225,
  kRsCityServiceConstructionDateFormatText = 247,
  kRsCityServiceIncomeFormatText = 281,
  kRsCityServiceResearchersFormatText = 295,
  kRsCityServiceQueryText = 1,
  kRsCityServiceElevationText = 2,
  kRsCityServiceYesText = 5,
  kRsCityServiceLandValueText = 8,
  kRsCityServiceCrimeText = 27,
  kRsCityServiceFlammabilityText = 34,
  kRsCityServiceMakeHistoricalText = 48,
  kRsCityServiceCleanUpCostText = 49,
  kRsCityServiceConnectedToText = 84,
  kRsCityServiceFundingCostText = 134,
  kRsCityServiceMonthlyValueFormatText = 135,
  kRsCityServiceNumberOfBedsSystemwideText = 167,
  kRsCityServiceNumberOfPatientsSystemwideText = 169,
  kRsCityServiceNumberOfDoctorsSystemwideText = 171,
  kRsCityServiceNumberOfDesksSystemwideText = 179,
  kRsCityServiceNumberOfStudentsSystemwideText = 181,
  kRsCityServiceNumberOfTeachersSystemwideText = 183,
  kRsCityServiceApplesForTheTeacherText = 191,
  kRsCityServiceCapacityText = 200,
  kRsCityServiceStudentsInAttendanceText = 202,
  kRsCityServiceNumberOfProfessorsText = 204,
  kRsCityServiceBooksSystemwideText = 212,
  kRsCityServiceBooksInCirculationText = 214,
  kRsCityServiceAttendanceText = 222,
  kRsCityServiceLastYearValueFormatText = 223,
  kRsCityServiceDinosaurBonesText = 224,
  kRsCityServiceLittleLeaguersText = 242,
  kRsCityServiceBoatsBerthedText = 244,
  kRsCityServiceDateBuiltText = 246,
  kRsCityServiceNumberOfEggsThrownText = 248,
  kRsCityServiceMilesOfRedTapeText = 250,
  kRsCityServiceNumberOfPigeonsText = 252,
  kRsCityServiceWinLossRecordText = 272,
  kRsCityServiceWinLossRecordFormatText = 273,
  kRsCityServiceSeasonTicketHoldersText = 274,
  kRsCityServiceMonthlyIncomeText = 280,
  kRsCityServiceAvgWaitForParkingSpaceText = 282,
  kRsCityServiceHoursFormatText = 283,
  kRsCityServiceLicensePlatesProducedText = 284,
  kRsCityServiceDriveThroughWeddingsPerformedText = 286,
  kRsCityServiceNewRecruitsText = 288,
  kRsCityServiceResearchersText = 294,
  kRsCityServiceShipsSavedText = 300,
  kRsCityServicePlaceOfOriginText = 306,
  kRsCityServiceNextShuttleToText = 308,
  kRsCityServiceCurrentProjectText = 319,
  kRsCityServiceLatestBreakthroughText = 330,
  kRsCityServiceNowAppearingText = 342,
  kRsCityServiceFavoriteAttractionText = 351,
  kRsCityServiceCurrentConversionProjectText = 361,
  kRsCityServiceMostPopularCourseText = 372,
  kRsCityServiceWorkersOnStrikeText = 390,
  kRsCityServiceTeamNameText = 429,
  kRsCityServiceDecommissionedText = 436,
  kRsCityServiceNoAccessToCityText = 440,
  kRsCityServiceMetersFormatText = 3,
  kRsCityServicePoweredText = 4,
  kRsCityServiceWateredText = 7,
  kRsCityServicePollutionGeneratedText = 41,
  kRsCityServicePassengersPerDayText = 55,
  kRsCityServiceCurrentWaterCapacityText = 94,
  kRsCityServiceMMonthFormatText = 95,
  kRsCityServiceEffectOfWaterPollutionText = 96,
  kRsCityServiceConnectedToCoastText = 102,
  kRsCityServiceSurfersCloggingIntakeText = 105,
  kRsCityServiceConnectedToPipeNetworkText = 115,
  kRsCityServiceCurrentDisposalCapacityText = 118,
  kRsCityServiceTonsMonthFormatText = 119,
  kRsCityServiceCapacityCitywideText = 268,
  kRsCityServiceMaximumWaterCapacityText = 384,
  kRsCityServiceServiceQualityText = 385,
  kRsCityServicePoorText = 389,
  kRsCityServiceMaximumDisposalCapacityText = 427,
  kRsCityServiceAgeMaxText = 437,
  kRsCityServiceYrsYrsFormatText = 438,
};

// Localized SupplementalGame text instances from the installed IXF catalog.
enum RsSupplementalGameTextId : u32 {
  kRsSupplementalGameGreetingsMayorText = 3,
  kRsSupplementalGameScenarioGoalsText = 4,
  kRsSupplementalGameScenarioCompletedText = 7,
  kRsSupplementalGameBeverlyOverlyText = 19,
  kRsSupplementalGameScenarioText = 36,
  kRsSupplementalGameMayorResultsFormatText = 37,
  kRsSupplementalGameLandscapeText = 40,
  kRsSupplementalGameTreesText = 41,
  kRsSupplementalGameBuildingsText = 42,
  kRsSupplementalGameUserMadeBuildingsText = 43,
  kRsSupplementalGameCityOptionsText = 44,
  kRsSupplementalGameDescriptionText = 46,
  kRsSupplementalGameSelectText = 47,
  kRsSupplementalGameViewSnapshotsText = 200,
  kRsSupplementalGameCannotDeleteSnapshotText = 202,
  kRsSupplementalGameNoDescriptionText = 210,
  kRsSupplementalGameReplaceText = 300,
  kRsSupplementalGameArchitectText = 306,
  kRsSupplementalGameSizeText = 307,
  kRsSupplementalGameBuildingSelectionCountFormatText = 312,
  kRsSupplementalGameConfirmClearReplacementsText = 321,
  kRsSupplementalGameCopyrightText = 407,
  kRsSupplementalGameAlbumsText = 408,
  kRsSupplementalGameDeleteText = 414,
  kRsSupplementalGameZoomInText = 415,
  kRsSupplementalGameBackText = 416,
  kRsSupplementalGameClearBuildingSetText = 703,
  kRsSupplementalGameAuthorText = 704,
  kRsSupplementalGameChangeInProgressText = 722,
  kRsSupplementalGameDevouredTreeText = 723,
};

// Localized Architect text instances from the installed IXF catalog.
enum RsArchitectTextId : u32 {
  kRsArchitectWallsText = 380,
  kRsArchitectRooftopsText = 381,
  kRsArchitectGroundText = 382,
  kRsArchitectTraditionalText = 390,
  kRsArchitectContemporaryText = 391,
  kRsArchitectNewBuildingTileSizeText = 424,
  kRsArchitectChooseTileSizeText = 480,
  kRsArchitectSelectRenderSetText = 502,
  kRsArchitectSaveBuildingText = 602,
  kRsArchitectConfirmSaveText = 603,
  kRsArchitectADivisionOfElectronicArtsTMText = 24,
  kRsArchitectMaxisText = 465,
  kRsArchitectNewBuildingText = 466,
  kRsArchitectErrorMessageText = 700,
  kRsArchitectOverwriteExistingFileText = 701,
  kRsArchitectThisFileIsReadOnlySaveTheText = 715,
  kRsArchitectTheFileNameIsNotValidReFormatText = 716,
  kRsArchitectInvalidFilePathPleaseTryAnotherText = 718,
};

// Localized ArchitectSupplemental text instances from the installed IXF catalog.
enum RsArchitectSupplementalTextId : u32 {
  kRsArchitectSupplementalDuplicateBuildingNameText = 14,
  kRsArchitectSupplementalConfirmRemoveBuildingText = 35,
  kRsArchitectSupplementalUpdateBuildingListText = 36,
  kRsArchitectSupplementalConfirmUpdateBuildingListText = 37,
  kRsArchitectSupplementalArchitectureText = 38,
  kRsArchitectSupplementalBuildingDecorationText = 39,
  kRsArchitectSupplementalPlazaAndStreetText = 40,
  kRsArchitectSupplementalYardObjectsText = 41,
  kRsArchitectSupplementalFloraText = 42,
  kRsArchitectSupplementalRooftopsText = 43,
  kRsArchitectSupplementalVehiclesText = 44,
  kRsArchitectSupplementalCustomPropsText = 45,
  kRsArchitectSupplementalIndustrialText = 47,
  kRsArchitectSupplementalBuildingArchitectPlusText = 48,
  kRsArchitectSupplementalColorsText = 50,
  kRsArchitectSupplementalSaveBeforeRenderText = 54,
  kRsArchitectSupplementalBuildingSizeText = 55,
};

// Localized Gui text instances from the installed IXF catalog.
enum RsGuiTextId : u32 {
  kRsGuiQueryText = 60,
  kRsGuiPopText = 64,
  kRsGuiRText = 65,
  kRsGuiCText = 66,
  kRsGuiIText = 67,
  kRsGuiNText = 68,
  kRsGuiPauseSimulationText = 100,
  kRsGuiRunSimulationText = 101,
  kRsGuiTurtleText = 102,
  kRsGuiLlamaText = 103,
  kRsGuiCheetahText = 104,
  kRsGuiAfricanSwallowText = 105,
  kRsGuiHideToolbarsText = 166,
  kRsGuiLayerViewsText = 167,
  kRsGuiShowToolbarsText = 169,
};

// Localized BuildingCatalog text instances from the installed IXF catalog.
enum RsBuildingCatalogTextId : u32 {
  kRsBuildingCatalogPowerMwhFormatText = 3,
  kRsBuildingCatalogCapacityCubicMetersFormatMonthText = 4,
  kRsBuildingCatalogAlreadyPlacedText = 112,
  kRsBuildingCatalogCapacityTonsFormatMonthText = 113,
  kRsBuildingCatalogBuildingsAvailableText = 118,
};

// Localized Message text instances from the installed IXF catalog.
enum RsMessageTextId : u32 {
  kRsMessageSaveText = 6,
  kRsMessageDoYouWantToSaveYourCityText = 18,
  kRsMessageSettingsRequireRestartText = 24,
  kRsMessageReadOnlySaveAnotherNameText = 38,
  kRsMessageInvalidFileNameFormatText = 39,
  kRsMessageErrorMessageText = 40,
  kRsMessageReadOnlyUseSaveAsText = 41,
  kRsMessageInsertGameDiscText = 43,
  kRsMessageNoCDText = 44,
};

// Localized TipOfDay text instances from the installed IXF catalog.
enum RsTipOfDayTextId : u32 {
  kRsTipOfDayTheLlamaSuggestsText = 59,
  kRsTipOfDayNextTipText = 60,
  kRsTipOfDayShowTipsAtStartupText = 61,
};

// Localized Menu text instances from the installed IXF catalog.
enum RsMenuTextId : u32 {
  kRsMenuLoadCityText = 2,
  kRsMenuPreferencesText = 4,
  kRsMenuExitText = 6,
};

// Additional localized News text instances from the installed text catalog.
enum RsNewsTextId : u32 {
  kRsNewsParadeCancelledNoStreetLongEnoughForText = 1005,
};

enum RsAdvisorNameTextId : u32 {
  kRsAdvisorNameUtilitiesAdvisorText = 81,
  kRsAdvisorNameFinancialAdvisorText = 82,
  kRsAdvisorNameHealthEducationAndAuraAdvisorText = 83,
  kRsAdvisorNameEnvironmentAdvisorText = 84,
  kRsAdvisorNamePublicSafetyAdvisorText = 85,
  kRsAdvisorNameTransportationAdvisorText = 86,
  kRsAdvisorNameCityPlannerText = 87,
};

// Image groups used by standard UI controls and advisor portraits.
enum RsImageGroup : u32 {
  kRsMediumUiImageGroup = 0x800600,
  kRsCompactUiImageGroup = 0x640480,
  kRsLargeUiImageGroup = 0x1024768,
  kRsDialogImageGroup = 0xc2bf3646,
  kRsMainImageGroup = 0x82b9b75c,
  kRsGonzoControlImageGroup = 0x42dec7d2,
  kRsOtherControlImageGroup = 0x22dec92d,
  kRsTerrainEditorImageGroup = 0x22dec90f,
  kRsMapImageGroup = 0x000005e5,
  kRsAdvisorTopicImageGroup = 0x831d112b,
  kRsCitySchemeImageGroup = 0x85535958,
  kRsUiImageGroup = 0x82b9b75b,
  kRsAdvisorPortraitGroup = 0x52db60a0,
};

// Vehicle attributes for street traffic, aircraft, ships, dependent wakes and street animation actors.
inline constexpr u32 kRsVehicleAttributesGroup = 0x025c2c45;

// Help-button atlas shared by petition, ordinance and budget windows.
enum RsHelpImageId : u32 {
  kRsHelpButtonImageId = 0x82d6f678,
};

// Standard UI push-button atlas within kRsUiImageGroup, shared by dialog and budget controls.
inline constexpr u32 kRsStandardButtonImageId = 0xa2defd9a;

// Copyright (C) 2016 Nelson Gomez
// Copyright (C) 2024 Nicholas Hayes
// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

struct RsKey {
  // Initialize the empty resource key. Zero words also form the indexed archive terminator.
  constexpr RsKey() : type(0), group(0), instance(0) {
  }

  // Keep the SDK constructor's type/group/instance order. Disk serialization retains its separate group-first order.
  constexpr RsKey(u32 type, u32 group, u32 instance) : type(type), group(group), instance(instance) {
  }

  u32 type, group, instance;
};

// The live DB-record interface consumed by factories. Its key is borrowed from the record.
class RsRecord : public ScUnknown {
public:
  virtual RsKey *GetKey() = 0;
};

class ScStringView;

// Resource ownership and key storage form the secondary base of concrete resource objects.
// Paired const/mutable accessors precede the reference query, debug tags and original slot60 deleting method.
class RsResource : public ScUnknown {
public:
  RsResource();
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  virtual const RsKey *GetKey() const;
  virtual RsKey *GetKey();
  virtual const u32 *Type() const;
  virtual u32 *Type();
  virtual const u32 *Group() const;
  virtual u32 *Group();
  virtual const u32 *Instance() const;
  virtual u32 *Instance();
  virtual u32 RefCount() const;
  virtual bool DebugInstanceTag(ScStringView *out);
  virtual bool DebugClassTag(ScStringView *out);
  virtual bool DebugTypeTag(ScStringView *out);
  virtual ~RsResource();
  RsKey key;
  u32 references;
};

// The installed module shares one recursive critical region across resources and factory loads.
void RsEnterResourceRegion();
void RsLeaveResourceRegion();

class GxBuffer;
class RsStream;
struct RsBitmapInfo;

// The original tag1/tag2 image header is five stored DWORDs, distinct from the three-word pixel header.
struct RsImageHeader {
  u32 tag, size, width, height;
  i32 format;
};

struct RsRasterHeader {
  u32 type, width, height;
};

// Original image identity22B9DA52, separate from its resource and independently owned graphics buffer.
class RsImageData : public ScUnknown {
public:
  virtual u8 SetBuffer(GxBuffer *buffer) = 0;
  virtual bool QueryBuffer(u32 iid, void **out) = 0;
  virtual bool QueryBuffer(GxBuffer **out) = 0;
  virtual u8 Read(RsStream *stream, const GxColorType *format, u32 mode) = 0;
  virtual u8 Write(RsStream *stream, u32 encoding) = 0;
};

// Actual image resource with the resource base first and its own image interface second.
class RsImageResource : public RsResource, public RsImageData {
public:
  RsImageResource();
  ~RsImageResource() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  u8 SetBuffer(GxBuffer *buffer) override;
  bool QueryBuffer(GxBuffer **out) override;
  bool QueryBuffer(u32 iid, void **out) override;
  u8 Read(RsStream *stream, const GxColorType *format, u32 mode) override;
  u8 Write(RsStream *stream, u32 encoding) override;

private:
  u8 ReadBmp(RsStream *stream, const GxColorType *format, u32 mode);
  u8 ReadRaw(RsStream *stream, const RsImageHeader *header, const GxColorType *format, u32 mode);
  u8 ReadQfs(RsStream *stream, const RsImageHeader *header, const GxColorType *format, u32 mode);
  u8 ReadPixels(RsStream *stream, const RsRasterHeader *header, const GxColorType *format, u32 mode);
  u8 UploadBmp(const RsBitmapInfo *info, const u8 *pixels);
  u8 Upload4To16(const RsBitmapInfo *info, const u8 *pixels);
  u8 Upload8To16(const RsBitmapInfo *info, const u8 *pixels);
  u8 Upload24To16(const RsBitmapInfo *info, const u8 *pixels);
  u8 Upload8To8(const RsBitmapInfo *info, const u8 *pixels);
  u8 Upload24To24(const RsBitmapInfo *info, const u8 *pixels);
  u8 Upload24To32(const RsBitmapInfo *info, const u8 *pixels);
  u8 WriteQfs(RsStream *stream);
  u8 WriteBmp(RsStream *stream);
  GxBuffer *buffer;
};

// Shared original QFS decoder entry also consumed by packed resource segments.
bool SC3_STDCALL RsDecodeImageQfs(const u8 *source, u32 size, u8 *destination, u32 *written);

// Copyright (C) 2025 Nicholas Hayes
// SDK declaration additions: Copyright Nicholas Hayes; LGPL-2.1-or-later.
// See THIRD_PARTY_NOTICES.txt for the upstream source.

class RsKeyListInterface : public ScUnknown {
public:
  virtual bool Insert(const RsKey &key) = 0;
  virtual bool Insert(const RsKeyListInterface &source) = 0;
  virtual bool Erase(const RsKey &key) = 0;
  virtual bool EraseAll() = 0;
  virtual void EnumKeys(void (*callback)(const RsKey &key, void *context), void *context) const = 0;
  virtual bool IsPresent(const RsKey &key) = 0;
  virtual u32 Size() const = 0;
  virtual RsKey *GetKey(u32 index) = 0;
};

// The original unique ordered key set and distinct module-reference base retain their genuine ownership.
class RsKeyList : public RsKeyListInterface, public RsModuleObject {
public:
  RsKeyList();
  ~RsKeyList() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
  bool Insert(const RsKey &key) override;
  bool Insert(const RsKeyListInterface &source) override;
  bool Erase(const RsKey &key) override;
  bool EraseAll() override;
  void EnumKeys(void (*callback)(const RsKey &key, void *context), void *context) const override;
  bool IsPresent(const RsKey &key) override;
  u32 Size() const override;
  RsKey *GetKey(u32 index) override;

private:
  struct RsKeyOrder {
    bool operator()(const RsKey &left, const RsKey &right) const;
  };

  static bool Less(const RsKey &left, const RsKey &right);
  static void InsertCallback(const RsKey &key, void *context);
  void InsertKey(const RsKey &key);
  std::set<RsKey, RsKeyOrder> keys;
  RsKey endKey; // Stable storage for the original end-node payload address returned at index Size().
};

// SDK constant; keep any verified Unlimited identity corrections explicit.
inline constexpr u32 kRsKeyListInterfaceClassId = 0x801ed267;

// The String and resource interfaces are distinct bases sharing the resource base's reference count.
class RsString : public ScString, public RsResource {
public:
  RsString();
  ~RsString() override;
  bool QueryInterface(u32 iid, void **out) override;
  u32 AddRef() override;
  u32 Release() override;
};

#endif
