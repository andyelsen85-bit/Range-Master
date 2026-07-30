CREATE TYPE "public"."maschine" AS ENUM('A', 'B', 'C', 'D', 'E', 'F', 'G', 'H');--> statement-breakpoint
CREATE TYPE "public"."modus" AS ENUM('NORMAL', 'HARAKIRI', 'HARAKIRI_DELAYED', 'HARAKIRI_FULL', 'CUSTOM_1', 'CUSTOM_2', 'CUSTOM_3');--> statement-breakpoint
CREATE TYPE "public"."kredit_typ" AS ENUM('GRANT', 'USE');--> statement-breakpoint
CREATE TYPE "public"."smtp_verschluesselung" AS ENUM('NONE', 'STARTTLS', 'SSL');--> statement-breakpoint
CREATE TYPE "public"."email_status" AS ENUM('NONE', 'PENDING', 'SENT', 'FAILED');--> statement-breakpoint
CREATE TYPE "public"."spieler_update_typ" AS ENUM('UPDATE', 'PASSWORT_RESET');--> statement-breakpoint
CREATE TABLE "spieler" (
	"id" serial PRIMARY KEY NOT NULL,
	"name" text NOT NULL,
	"email" text,
	"mitglied_nr" text,
	"portal_aktiv" boolean DEFAULT false NOT NULL,
	"is_admin" boolean DEFAULT false NOT NULL,
	"passwort_hash" text,
	"eingeladen_at" timestamp with time zone,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "spieler_email_unique" UNIQUE("email"),
	CONSTRAINT "spieler_mitglied_nr_unique" UNIQUE("mitglied_nr")
);
--> statement-breakpoint
CREATE TABLE "spiele" (
	"id" serial PRIMARY KEY NOT NULL,
	"external_id" text,
	"datum" timestamp with time zone NOT NULL,
	"modus" "modus" DEFAULT 'NORMAL' NOT NULL,
	"lauf" integer NOT NULL,
	"tauben_pro_lauf" integer DEFAULT 9 NOT NULL,
	"abgeschlossen" boolean DEFAULT false NOT NULL,
	"synced_at" timestamp with time zone,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "spiele_external_id_unique" UNIQUE("external_id")
);
--> statement-breakpoint
CREATE TABLE "spiel_teilnahmen" (
	"id" serial PRIMARY KEY NOT NULL,
	"spiel_id" integer NOT NULL,
	"spieler_id" integer NOT NULL,
	"start_posten" integer NOT NULL,
	"punkte" integer DEFAULT 0 NOT NULL,
	"lauf" integer NOT NULL,
	CONSTRAINT "spiel_teilnahmen_spiel_id_spieler_id_lauf_unique" UNIQUE("spiel_id","spieler_id","lauf")
);
--> statement-breakpoint
CREATE TABLE "ergebnisse" (
	"id" serial PRIMARY KEY NOT NULL,
	"spiel_id" integer NOT NULL,
	"spieler_id" integer NOT NULL,
	"lauf" integer NOT NULL,
	"taube" integer NOT NULL,
	"maschine" "maschine" NOT NULL,
	"posten" integer NOT NULL,
	"schuss1" boolean DEFAULT false NOT NULL,
	"schuss2" boolean DEFAULT false NOT NULL,
	"punkte" integer DEFAULT 0 NOT NULL,
	"wiederholt" boolean DEFAULT false NOT NULL
);
--> statement-breakpoint
CREATE TABLE "api_keys" (
	"id" serial PRIMARY KEY NOT NULL,
	"name" text NOT NULL,
	"key" text NOT NULL,
	"type" text NOT NULL,
	"active" boolean DEFAULT true NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "api_keys_key_unique" UNIQUE("key")
);
--> statement-breakpoint
CREATE TABLE "kredit_events" (
	"id" serial PRIMARY KEY NOT NULL,
	"external_id" text NOT NULL,
	"spieler_id" integer NOT NULL,
	"datum" date NOT NULL,
	"typ" "kredit_typ" NOT NULL,
	"anzahl" integer NOT NULL,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "kredit_events_external_id_unique" UNIQUE("external_id")
);
--> statement-breakpoint
CREATE TABLE "smtp_settings" (
	"id" serial PRIMARY KEY NOT NULL,
	"host" text DEFAULT '' NOT NULL,
	"port" integer DEFAULT 587 NOT NULL,
	"username" text DEFAULT '' NOT NULL,
	"passwort" text DEFAULT '' NOT NULL,
	"from_address" text DEFAULT '' NOT NULL,
	"verschluesselung" "smtp_verschluesselung" DEFAULT 'STARTTLS' NOT NULL,
	"ignore_tls_errors" boolean DEFAULT false NOT NULL,
	"portal_url" text DEFAULT '' NOT NULL,
	"updated_at" timestamp with time zone DEFAULT now() NOT NULL
);
--> statement-breakpoint
CREATE TABLE "spieler_updates" (
	"id" serial PRIMARY KEY NOT NULL,
	"external_id" text NOT NULL,
	"spieler_id" integer NOT NULL,
	"typ" "spieler_update_typ" NOT NULL,
	"email_status" "email_status" DEFAULT 'NONE' NOT NULL,
	"email_error" text,
	"created_at" timestamp with time zone DEFAULT now() NOT NULL,
	CONSTRAINT "spieler_updates_external_id_unique" UNIQUE("external_id")
);
--> statement-breakpoint
ALTER TABLE "spiel_teilnahmen" ADD CONSTRAINT "spiel_teilnahmen_spiel_id_spiele_id_fk" FOREIGN KEY ("spiel_id") REFERENCES "public"."spiele"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "spiel_teilnahmen" ADD CONSTRAINT "spiel_teilnahmen_spieler_id_spieler_id_fk" FOREIGN KEY ("spieler_id") REFERENCES "public"."spieler"("id") ON DELETE no action ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "ergebnisse" ADD CONSTRAINT "ergebnisse_spiel_id_spiele_id_fk" FOREIGN KEY ("spiel_id") REFERENCES "public"."spiele"("id") ON DELETE cascade ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "ergebnisse" ADD CONSTRAINT "ergebnisse_spieler_id_spieler_id_fk" FOREIGN KEY ("spieler_id") REFERENCES "public"."spieler"("id") ON DELETE no action ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "kredit_events" ADD CONSTRAINT "kredit_events_spieler_id_spieler_id_fk" FOREIGN KEY ("spieler_id") REFERENCES "public"."spieler"("id") ON DELETE no action ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "spieler_updates" ADD CONSTRAINT "spieler_updates_spieler_id_spieler_id_fk" FOREIGN KEY ("spieler_id") REFERENCES "public"."spieler"("id") ON DELETE no action ON UPDATE no action;--> statement-breakpoint
CREATE INDEX "kredit_events_datum_idx" ON "kredit_events" USING btree ("datum");--> statement-breakpoint
CREATE INDEX "spieler_updates_spieler_idx" ON "spieler_updates" USING btree ("spieler_id");